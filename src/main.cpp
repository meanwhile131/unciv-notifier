#include "overload.h"
#include <chrono>
#include <cstddef>
#include <curl/curl.h>
#include <curl/easy.h>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <shared_mutex>
#include <string>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <thread>
#include <toml++/toml.hpp>
#include <utility>
#include <zlib.h>
#include <cstdint>

using json = nlohmann::json;

namespace td_api = td::td_api;
using namespace std::chrono_literals;

namespace {
	auto write_callback(char *ptr, size_t  /*size*/, size_t nmemb, void *userdata) -> size_t {
		auto *data = static_cast<std::string*>(userdata);
		data->append(ptr, nmemb);
		return nmemb;
	}

	class UncivNotifier {
		using RequestCallback = std::function<void(td_api::object_ptr<td_api::Object>)>;
		std::unique_ptr<td::ClientManager> client_manager;
		td::ClientManager::ClientId client_id{};
		CURL *handle = curl_easy_init();
		std::jthread stateCheckThread;
		std::string notification;
		td_api::int53 chat_id{};
		std::string uuid;

		std::shared_mutex requestMutex;
		td::ClientManager::RequestId requestId = 1;
		std::unordered_map<td::ClientManager::RequestId, RequestCallback> requestCallbacks;

		std::chrono::steady_clock::duration start_notify_interval;
		std::chrono::steady_clock::duration notify_interval = start_notify_interval;
		std::chrono::steady_clock::time_point last_notify;
		unsigned int turn_count{};

		std::chrono::hours start_night;
		std::chrono::hours end_night;
		unsigned int max_night_messages;
		unsigned int night_messages = 0;

		const td_api::int32 API_ID = 94575;
		const td_api::string API_HASH = "a3406de8d171bb422bb6ddf3bbd800e2";

		public:
		UncivNotifier(const UncivNotifier &) = delete;
		UncivNotifier(UncivNotifier &&) = delete;
		auto operator=(const UncivNotifier &) -> UncivNotifier & = delete;
		auto operator=(UncivNotifier &&) -> UncivNotifier & = delete;
		UncivNotifier(
				const std::string &previewUrl,
				td_api::object_ptr<td_api::proxy> proxy,
				std::string notification, td_api::int53 chat_id,
				std::string uuid, std::chrono::hours start_night,
				unsigned int max_night_messages,
				std::chrono::hours end_night,
				std::chrono::steady_clock::duration start_notify_interval)
			: client_manager(std::make_unique<td::ClientManager>()), client_id(client_manager->create_client_id()), notification(std::move(std::move(notification))),
			uuid(std::move(std::move(uuid))), chat_id(chat_id),
			start_night(start_night), end_night(end_night),
			max_night_messages(max_night_messages),
			start_notify_interval(start_notify_interval) {
				curl_easy_setopt(handle, CURLOPT_URL, previewUrl.c_str());
				curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION,
						write_callback);

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
		~UncivNotifier() {
			curl_easy_cleanup(handle);
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
			std::cout << "Checking game state" << '\n';
			std::string data;
			curl_easy_setopt(handle, CURLOPT_WRITEDATA, &data);
			CURLcode const code = curl_easy_perform(handle);
			std::vector<unsigned char> data_vector(data.begin(), data.end());

			std::vector<unsigned char> inbuf(data.size()); // data always smaller than base64 
			int size = EVP_DecodeBlock(inbuf.data(), data_vector.data(), static_cast<int>(data_vector.size()));
			inbuf.resize(size);

			z_stream strm;
			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;
			strm.total_in = 0;
			strm.next_in = inbuf.data();
			strm.avail_in = inbuf.size();
			int ret = inflateInit2(&strm, MAX_WBITS + 16);
			if (ret != Z_OK) {
				std::cout << "inflateInit: " << ret << '\n';
			}
			const size_t BUFFER_SIZE = 32768;
			std::vector<unsigned char> buffer(BUFFER_SIZE);
			std::vector<unsigned char> decompressed;
			while (ret != Z_STREAM_END) {
				strm.next_out = buffer.data();
				strm.avail_out = buffer.size();
				ret = inflate(&strm, Z_NO_FLUSH);
				if (ret != Z_OK && ret != Z_STREAM_END) {
					std::cout << "inflate: " << ret << '\n';
					break;
				}
				size_t have = buffer.size() - strm.avail_out;
				decompressed.insert(decompressed.end(), buffer.begin(), buffer.begin() + static_cast<int64_t>(have));
			}
			inflateEnd(&strm);
			json game = json::parse(decompressed);
			std::map<std::string, std::string> playerIDs;
			for (json civilization : game["civilizations"]) {
				if (civilization.contains("playerId")) {
					playerIDs[civilization["civID"]] = civilization["playerId"];
				}
			}
			std::string currentCiv = game["currentPlayer"];
			std::cout << "Current turn is " << currentCiv << '\n';
			bool new_turn = game["turns"] > turn_count;
			if (new_turn) {
				notify_interval = start_notify_interval;
			}
			if (playerIDs.contains(currentCiv)) {
				std::string player = playerIDs[currentCiv];
				auto now = std::chrono::steady_clock::now();
				auto since_last_notify = now - last_notify;
				bool should_notify = (since_last_notify >= notify_interval) || new_turn;
				if (player == uuid && should_notify) {
					std::cout << "notifying " + currentCiv << " (" << chat_id << "), next notify after " << notify_interval << '\n';
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
			turn_count = game["turns"];
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
} // namespace

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
