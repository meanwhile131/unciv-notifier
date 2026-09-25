#include "overload.hpp"
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
#include "game.hpp"
#include "user.hpp"

using json = nlohmann::json;

namespace td_api = td::td_api;
using namespace std::chrono_literals;
using namespace std::literals;

using uuid = std::string;
using Users = std::map<uuid, std::shared_ptr<User>>;
namespace {
	class UncivNotifier {
		using RequestCallback = std::function<void(td_api::object_ptr<td_api::Object>)>;
		std::unique_ptr<td::ClientManager> client_manager;
		td::ClientManager::ClientId client_id{};
		std::jthread stateCheckThread;
		std::string notification;
		td_api::int53 chat_id{};
		std::vector<Game> games;
		Users users;

		std::shared_mutex requestMutex;
		td::ClientManager::RequestId requestId = 1;
		std::unordered_map<td::ClientManager::RequestId, RequestCallback> requestCallbacks;

		unsigned int night_messages = 0;

		const td_api::int32 API_ID = 94575;
		const td_api::string API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";

		public:
		UncivNotifier(
				td_api::object_ptr<td_api::proxy> proxy,
				std::vector<Game> games,
				Users users)
			: client_manager(std::make_unique<td::ClientManager>()),
			client_id(client_manager->create_client_id()),
			games(std::move(games)),
			users(std::move(users))
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
			for (auto &game : games) {
				game.update();
				Civilization civilization = game.getCurrentPlayer();
				std::cout << "Current turn is " << civilization.civID << '\n';
				auto user = users[civilization.playerId];
				if (!user) {
					std::cout << "No user found for " << civilization.playerId << "\n";
					continue;
				}
				auto notification = user->notifyIfNeeded(game.isNewTurn());
				if (notification.has_value()) {
					std::cout << "notifying " + civilization.civID << " (" << user->getChatID() << "), next notify after " << user->getNotifyInterval() << '\n';
					send_query(std::move(notification.value()));
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
				}));

		}
	};
}  // namespace

auto main() -> int {
	auto config = toml::parse_file("config.toml");
	curl_global_init(CURL_GLOBAL_ALL);
	td_api::object_ptr<td_api::proxy> proxy;
	if (config.contains("proxy")) {
		proxy = td_api::make_object<td_api::proxy>(
				config["proxy"]["host"].value_or(""),
				config["proxy"]["port"].value_or(0),
				td_api::make_object<td_api::proxyTypeMtproto>(config["proxy"]["secret"].value_or("")));
	}
	Users users;
	for (auto &node : *config["notify"]["users"].as_array()) {
		auto user_config = *node.as_table();
		std::string uuid = user_config["uuid"].value_or("");
		auto user = std::make_shared<User>(
				user_config["chat_id"].value_or(0),
				std::chrono::minutes(user_config["start_notify_interval"].value_or(0)),
				user_config["text"].value_or(""),
				std::chrono::hours(user_config["start_night"].value_or(0)),
				user_config["max_night_messages"].value_or(0),
				std::chrono::hours(user_config["end_night"].value_or(0)));
		users[uuid] = std::move(user);
	}
	std::vector<Game> games;
	for (auto &url_node : *config["game"]["urls"].as_array()) {
		std::string url = url_node.value_or(""s);
		std::cout << "Adding game with URL: " << url << "\n";
		Game game(url);
		games.push_back(std::move(game));
	}
	UncivNotifier app(std::move(proxy), std::move(games), std::move(users));
	app.loop();
}
