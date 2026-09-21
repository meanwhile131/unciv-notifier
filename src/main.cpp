#include "overload.h"
#include <chrono>
#include <curl/curl.h>
#include <curl/easy.h>
#include <iostream>
#include <mutex>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <thread>
#include <toml++/toml.hpp>
#include <utility>
#include <zlib.h>
#include "game.h"

using json = nlohmann::json;

namespace td_api = td::td_api;
using namespace std::chrono_literals;

namespace {
	class UncivNotifier {
		using RequestCallback = std::function<void(td_api::object_ptr<td_api::Object>)>;
		std::unique_ptr<td::ClientManager> client_manager;
		td::ClientManager::ClientId client_id{};
		std::jthread stateCheckThread;
		std::string notification;
		td_api::int53 chat_id{};
		std::string uuid;
		Game current_game;

		std::shared_mutex requestMutex;
		td::ClientManager::RequestId requestId = 1;
		std::unordered_map<td::ClientManager::RequestId, RequestCallback> requestCallbacks;

		std::chrono::steady_clock::duration start_notify_interval;
		std::chrono::steady_clock::duration notify_interval = start_notify_interval;
		std::chrono::steady_clock::time_point last_notify;

		std::chrono::hours start_night;
		std::chrono::hours end_night;
		unsigned int max_night_messages;
		unsigned int night_messages = 0;

		const td_api::int32 API_ID = 94575;
		const td_api::string API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";

		public:
		UncivNotifier(
				const std::string &previewUrl,
				td_api::object_ptr<td_api::proxy> proxy,
				std::string notification, td_api::int53 chat_id,
				std::string uuid, std::chrono::hours start_night,
				unsigned int max_night_messages,
				std::chrono::hours end_night,
				std::chrono::steady_clock::duration start_notify_interval)
			: client_manager(std::make_unique<td::ClientManager>()), client_id(client_manager->create_client_id()), notification(std::move(std::move(notification))),
			current_game(previewUrl), uuid(std::move(std::move(uuid))), chat_id(chat_id),
			start_night(start_night), end_night(end_night),
			max_night_messages(max_night_messages),
			start_notify_interval(start_notify_interval) {

				td::ClientManager::execute(
						td_api::make_object<td_api::setLogVerbosityLevel>(1));

				send_query(
						td_api::make_object<td_api::getOption>("version"));

				if (proxy) {
					auto request = td_api::make_object<td_api::addProxy>(
							std::move(proxy), true, "");
					send_query(std::move(request));
				}
			}
		void loop() {
			while (true) {
				const double UPDATE_TIMEOUT = 5;
				auto update = client_manager->receive(UPDATE_TIMEOUT);
				if (update.request_id != 0) {
					RequestCallback callback;
					{
						std::shared_lock lock(requestMutex);
						callback = requestCallbacks[update.request_id];
					}
					callback(std::move(update.object));
				}
				else if (update.object) {
					handleUpdate(std::move(update.object));
				}
			}
		}
		private:
		void checkStateThread() {
			while (true) {
				const std::chrono::seconds GAME_CHECK_INTERVAL(10);
				checkGameState();
				std::this_thread::sleep_for(GAME_CHECK_INTERVAL);
			}
		}
		void send_query(td_api::object_ptr<td_api::Function> request) {
			send_query(std::move(request), [](auto req) -> auto{});
		}
		void send_query(td_api::object_ptr<td_api::Function> request, RequestCallback callback) {
			std::unique_lock lock(requestMutex);
			client_manager->send(client_id, requestId, std::move(request));
			requestCallbacks[requestId] = std::move(callback);
			requestId++;
		}
		void checkGameState() {
			auto now = std::chrono::utc_clock::now();
			auto days = std::chrono::floor<std::chrono::days>(now);
			std::chrono::hh_mm_ss time_of_day(now - days);
			auto hours = time_of_day.hours();
			bool overnight = start_night > end_night;
			bool is_night_overnight = hours >= start_night || hours <= end_night;
			bool is_night_day = hours >= start_night && hours <= end_night;
			bool const is_night = overnight ? is_night_overnight : is_night_day;
			if (is_night && night_messages >= max_night_messages) {
				std::cout << "Skipping game checks because night and max message count reached" << '\n';
				return;
			}
			if (!is_night && night_messages != 0) {
				std::cout << "Resetting night message count" << '\n';
				night_messages = 0;
			}
			current_game.update();
			Civilization player = current_game.getCurrentPlayer();
			std::cout << "Current turn is " << player.civID << '\n';
			bool new_turn = current_game.isNewTurn();
			if (new_turn) {
				notify_interval = start_notify_interval;
			}
			if (player.playerId == uuid) {
				auto now = std::chrono::steady_clock::now();
				auto since_last_notify = now - last_notify;
				bool should_notify = (since_last_notify >= notify_interval) || new_turn;
				if (should_notify) {
					std::cout << "notifying " + player.civID << " (" << chat_id << "), next notify after " << notify_interval << '\n';
					auto request = td_api::make_object<td_api::sendMessage>();
					request->chat_id_ = chat_id;
					auto message = td_api::make_object<td_api::inputMessageText>();
					auto text = td_api::make_object<td_api::formattedText>();
					text->text_ = notification;
					message->text_ = std::move(text);
					request->input_message_content_ = std::move(message);
					notify_interval *= 2;
					last_notify = now;
					if (is_night) {
						night_messages++;
						std::cout << "Used " << night_messages << "/" << max_night_messages << " night messages" << '\n';
					}
					send_query(std::move(request));
				}
			}
		}
		void handleUpdate(td_api::object_ptr<td_api::Object> update) {
			td_api::downcast_call(*update, overloaded(
						[this](td_api::updateAuthorizationState &state) -> void {
						td_api::downcast_call(*state.authorization_state_, overloaded(
									[this](td_api::authorizationStateWaitTdlibParameters &) -> void {
									auto request = td_api::make_object<td_api::setTdlibParameters>();
									request->api_id_ = API_ID;
									request->api_hash_ = API_HASH;
									request->system_language_code_ = "en";
									request->device_model_ = "Desktop";
									request->application_version_ = "1.0";
									send_query(std::move(request));
									},
									[this](td_api::authorizationStateWaitPhoneNumber &) -> void {
									auto request = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
									std::string phone;
									std::cout << "Phone number: " << std::flush;
									std::getline(std::cin, phone);
									request->phone_number_ = phone;
									send_query(std::move(request));
									},
									[this](td_api::authorizationStateWaitCode &) -> void {
									auto request = td_api::make_object<td_api::checkAuthenticationCode>();
									std::string code;
									std::cout << "Code: " << std::flush;
									std::getline(std::cin, code);
									request->code_ = code;
									send_query(std::move(request));
									},
									[this](td_api::authorizationStateReady &) -> void {
										auto request = td_api::make_object<td_api::loadChats>();
										request->limit_ = std::numeric_limits<td_api::int32>::max();
										send_query(std::move(request), [this](auto  /*request*/) -> auto {
												stateCheckThread = std::jthread(&UncivNotifier::checkStateThread, this);
												});
									},
									[](auto &upd) -> auto {
										std::cout << td_api::to_string(upd) << std::endl;
									}));
						},
				[](auto &upd) -> auto {
					std::cout << td_api::to_string(upd) << std::endl;
				}));

		}
	};
}  // namespace

auto main() -> int {
	auto config = toml::parse_file("config.toml");
	curl_global_init(CURL_GLOBAL_ALL);
	std::string url = config["game"]["url"].value_or("");
	td_api::object_ptr<td_api::proxy> proxy;
	if (config.contains("proxy")) {
		proxy = td_api::make_object<td_api::proxy>(
				config["proxy"]["host"].value_or(""),
				config["proxy"]["port"].value_or(0),
				td_api::make_object<td_api::proxyTypeMtproto>(config["proxy"]["secret"].value_or("")));
	}
	UncivNotifier app(url, std::move(proxy),
			config["notify"]["text"].value_or(""),
			config["notify"]["chat_id"].value_or(0),
			config["notify"]["uuid"].value_or(""),
			std::chrono::hours(config["notify"]["start_night"].value_or(0)),
			config["notify"]["max_night_messages"].value_or(0),
			std::chrono::hours(config["notify"]["end_night"].value_or(0)),
			std::chrono::minutes(config["notify"]["start_notify_interval"].value_or(0)));
	app.loop();
}
