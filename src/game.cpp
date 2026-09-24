#include "game.h"
#include <iostream>
#include <utility>
#include <openssl/evp.h>
#include <zlib.h>

using json = nlohmann::json;

namespace {
	auto write_callback(char *ptr, size_t  /*size*/, size_t nmemb, void *userdata) -> size_t {
		auto *data = static_cast<std::string*>(userdata);
		data->append(ptr, nmemb);
		return nmemb;
	}
}

Game::Game(const std::string& previewUrl, const std::vector<User>& users) : handle(curl_easy_init(), curl_easy_cleanup), users(users) {
	curl_easy_setopt(&handle, CURLOPT_URL, previewUrl.c_str());
	curl_easy_setopt(&handle, CURLOPT_WRITEFUNCTION, write_callback);
}
void Game::update() {
	json game = fetch();
	new_turn = game["turns"] > turn_count;
	turn_count = game["turns"]; 

	current_player = *std::ranges::find_if(game["civilizations"].begin(), game["civilizations"].end(), [&game](auto &civ) -> bool { return civ["civID"] == game["currentPlayer"];});
}
auto Game::getNotifications() -> std::vector<td_api::object_ptr<td_api::sendMessage>> {
	std::vector<td_api::object_ptr<td_api::sendMessage>> notifications;
	for (auto user : users) {
		if (user.getUUID() == current_player.playerId) {
			auto notification = user.notifyIfNeeded(new_turn);
			if (notification.has_value()) {
				std::cout << "notifying " + current_player.civID << " (" << user.getChatID() << "), next notify after " << user.getNotifyInterval() << '\n';
				notifications.push_back(std::move(notification.value()));
			}
		}
	}
	return notifications;
}
auto Game::isNewTurn() const -> bool {
	return new_turn;
}
auto Game::getCurrentPlayer() -> Civilization {
	return current_player;
}
auto Game::fetch() -> json {
	std::string data;
	curl_easy_setopt(&handle, CURLOPT_WRITEDATA, &data);
	CURLcode const code = curl_easy_perform(&handle);
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
	const size_t BUFFER_SIZE = 32768;
	std::vector<unsigned char> buffer(BUFFER_SIZE);
	std::vector<unsigned char> decompressed;
	while (ret != Z_STREAM_END) {
		strm.next_out = buffer.data();
		strm.avail_out = buffer.size();
		ret = inflate(&strm, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END) {
			break;
		}
		size_t have = buffer.size() - strm.avail_out;
		decompressed.insert(decompressed.end(), buffer.begin(), buffer.begin() + static_cast<int64_t>(have));
	}
	inflateEnd(&strm);
	return json::parse(decompressed);
}
