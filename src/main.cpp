#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <curl/curl.h>
#include <curl/easy.h>
#include <openssl/evp.h>
#include <zconf.h>
#include <zlib.h>
#include "overload.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <functional>
#include <limits>
#include <toml++/toml.hpp>
#include <chrono>

using json = nlohmann::json;

namespace td_api = td::td_api;
using namespace std::chrono_literals;

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
	auto data = static_cast<std::string*>(userdata);
	data->append(ptr, nmemb);
	return nmemb;
}

class UncivNotifier {
	using RequestCallback = std::function<void(td_api::object_ptr<td_api::Object>)>;
	std::unique_ptr<td::ClientManager> client_manager;
	td::ClientManager::ClientId client_id;
	CURL *handle = curl_easy_init();
	std::jthread stateCheckThread;
	std::string notification;
	td_api::int53 chat_id;
	std::string uuid;

	std::shared_mutex requestMutex;
	td::ClientManager::RequestId requestId = 1;
	std::unordered_map<td::ClientManager::RequestId, RequestCallback> requestCallbacks;

	std::chrono::steady_clock::duration start_notify_interval;
	std::chrono::steady_clock::duration notify_interval = start_notify_interval;
	std::chrono::steady_clock::time_point last_notify;
	unsigned int turn_count;

	std::chrono::hours start_night;
	std::chrono::hours end_night;
	unsigned int max_night_messages;
	unsigned int night_messages = 0;

	public:
	UncivNotifier(std::string previewUrl, td_api::object_ptr<td_api::proxy> proxy, std::string notification, std::string uuid, td_api::int53 chat_id, std::chrono::hours start_night, std::chrono::hours end_night, unsigned int max_night_messages, std::chrono::steady_clock::duration start_notify_interval) : notification(notification), uuid(uuid), chat_id(chat_id), start_night(start_night), end_night(end_night), max_night_messages(max_night_messages), start_notify_interval(start_notify_interval) {
		curl_easy_setopt(handle, CURLOPT_URL, previewUrl.c_str());
		curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);

		td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
		client_manager = std::make_unique<td::ClientManager>();
		client_id = client_manager->create_client_id();
		send_query(td_api::make_object<td_api::getOption>("version"));
		
		if (proxy) {
			auto request = td_api::make_object<td_api::addProxy>(std::move(proxy), true, "");
			send_query(std::move(request));
		}
	}
	void loop() {
		while (true) {
			auto update = client_manager->receive(5);
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
			checkGameState();
			std::this_thread::sleep_for(std::chrono::seconds(10));
		}
	}
	void send_query(td_api::object_ptr<td_api::Function> request) {
		send_query(std::move(request), [](auto req){});
	}
	void send_query(td_api::object_ptr<td_api::Function> request, RequestCallback callback) {
		std::unique_lock lock(requestMutex);
		client_manager->send(client_id, requestId, std::move(request));
		requestCallbacks[requestId] = callback;
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
		bool is_night = overnight ? is_night_overnight : is_night_day;
		if (is_night && night_messages >= max_night_messages) {
			std::cout << "Skipping game checks because night and max message count reached" << std::endl;
			return;
		}
		if (!is_night && night_messages != 0) {
			std::cout << "Resetting night message count" << std::endl;
			night_messages = 0;
		}
		std::cout << "Checking game state" << std::endl;
		std::string data;
		curl_easy_setopt(handle, CURLOPT_WRITEDATA, &data);
		CURLcode code = curl_easy_perform(handle);

		std::vector<unsigned char> inbuf(data.size()); // data always smaller than base64 
		int size = EVP_DecodeBlock(inbuf.data(), reinterpret_cast<const unsigned char *>(data.c_str()), inbuf.size());
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
			std::cout << "inflateInit: " << ret << std::endl;
		}
		std::vector<unsigned char> buffer(32768);
		std::vector<unsigned char> decompressed;
		while (ret != Z_STREAM_END) {
			strm.next_out = buffer.data();
			strm.avail_out = buffer.size();
			ret = inflate(&strm, Z_NO_FLUSH);
			if (ret != Z_OK && ret != Z_STREAM_END) {
				std::cout << "inflate: " << ret << std::endl;
				break;
			}
			int have = buffer.size() - strm.avail_out;
			decompressed.insert(decompressed.end(), buffer.begin(), buffer.begin() + have);
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
		std::cout << "Current turn is " << currentCiv << std::endl;
		bool new_turn = game["turns"] > turn_count;
		if (new_turn) {
			notify_interval = start_notify_interval;
		}
		if (playerIDs.count(currentCiv) > 0) {
			std::string player = playerIDs[currentCiv];
			auto now = std::chrono::steady_clock::now();
			auto since_last_notify = now - last_notify;
			bool should_notify = (since_last_notify >= notify_interval) || new_turn;
			if (player == uuid && should_notify) {
				std::cout << "notifying " + currentCiv << " (" << chat_id << "), next notify after " << notify_interval << std::endl;
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
					std::cout << "Used " << night_messages << "/" << max_night_messages << " night messages" << std::endl;
				}
				send_query(std::move(request));
			}
		}
		turn_count = game["turns"];
	}
	void handleUpdate(td_api::object_ptr<td_api::Object> update) {
		td_api::downcast_call(*update, overloaded(
					[this](td_api::updateAuthorizationState &state) {
					td_api::downcast_call(*state.authorization_state_, overloaded(
								[this](td_api::authorizationStateWaitTdlibParameters &) {
								auto request = td_api::make_object<td_api::setTdlibParameters>();
								request->api_id_ = 94575;
								request->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
								request->system_language_code_ = "en";
								request->device_model_ = "Desktop";
								request->application_version_ = "1.0";
								send_query(std::move(request));
								},
								[this](td_api::authorizationStateWaitPhoneNumber &) {
								auto request = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
								std::string phone;
								std::cout << "Phone number: " << std::flush;
								std::getline(std::cin, phone);
								request->phone_number_ = phone;
								send_query(std::move(request));
								},
								[this](td_api::authorizationStateWaitCode &) {
								auto request = td_api::make_object<td_api::checkAuthenticationCode>();
								std::string code;
								std::cout << "Code: " << std::flush;
								std::getline(std::cin, code);
								request->code_ = code;
								send_query(std::move(request));
								},
								[this](td_api::authorizationStateReady &) {
									auto request = td_api::make_object<td_api::loadChats>();
									request->limit_ = std::numeric_limits<td_api::int32>::max();
									send_query(std::move(request), [this](auto request) {
											stateCheckThread = std::jthread(&UncivNotifier::checkStateThread, this);
											});
								},
								[](auto &upd) {
									std::cout << td_api::to_string(upd) << std::endl;
								}));
					},
			[](auto &upd) {
				std::cout << td_api::to_string(upd) << std::endl;
			}));

	}
};

int main() {
	auto config = toml::parse_file("config.toml");
	curl_global_init(CURL_GLOBAL_ALL);
	std::string url = config["game"]["url"].value_or("");
	td_api::object_ptr<td_api::proxy> proxy;
	if (config.contains("proxy"))
		proxy = td_api::make_object<td_api::proxy>(
				config["proxy"]["host"].value_or(""),
				config["proxy"]["port"].value_or(0),
				td_api::make_object<td_api::proxyTypeMtproto>(config["proxy"]["secret"].value_or("")));
	UncivNotifier app(url, std::move(proxy),
			config["notify"]["text"].value_or(""),
			config["notify"]["uuid"].value_or(""),
			config["notify"]["chat_id"].value_or(0),
			std::chrono::hours(config["notify"]["start_night"].value_or(0)),
			std::chrono::hours(config["notify"]["end_night"].value_or(0)),
			config["notify"]["max_night_messages"].value_or(0),
			std::chrono::minutes(config["notify"]["start_notify_interval"].value_or(0)));
	app.loop();
}
