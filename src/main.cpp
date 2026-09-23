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
#include "user.h"

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
		Game current_game;
		User user;

		std::shared_mutex requestMutex;
		td::ClientManager::RequestId requestId = 1;
		std::unordered_map<td::ClientManager::RequestId, RequestCallback> requestCallbacks;

		unsigned int night_messages = 0;

		const td_api::int32 API_ID = 94575;
		const td_api::string API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";

		public:
		UncivNotifier(
				const std::string &previewUrl,
				td_api::object_ptr<td_api::proxy> proxy,
				User user)
			: client_manager(std::make_unique<td::ClientManager>()),
			client_id(client_manager->create_client_id()),
			current_game(previewUrl),
			user(std::move(user))
		{

			td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));

			send_query(td_api::make_object<td_api::getOption>("version"));

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
			current_game.update();
			Civilization player = current_game.getCurrentPlayer();
			std::cout << "Current turn is " << player.civID << '\n';
			bool new_turn = current_game.isNewTurn();
			if (player.playerId == user.getUUID()) {
				auto request = user.notifyIfNeeded(new_turn).value_or(nullptr);
				if (request) {
					std::cout << "notifying " + player.civID << " (" << chat_id << "), next notify after " << user.getNotifyInterval() << '\n';
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
	User user(
			config["notify"]["chat_id"].value_or(0),
			config["notify"]["uuid"].value_or(""),
			std::chrono::minutes(config["notify"]["start_notify_interval"].value_or(0)),
			config["notify"]["text"].value_or(""),
			std::chrono::hours(config["notify"]["start_night"].value_or(0)),
			config["notify"]["max_night_messages"].value_or(0),
			std::chrono::hours(config["notify"]["end_night"].value_or(0)));
	UncivNotifier app(url,
			std::move(proxy),
			user,
			);
	app.loop();
}
