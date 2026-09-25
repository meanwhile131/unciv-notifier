#include "game.h"
#include <curl/curl.h>
#include <iostream>
#include <utility>
#include <openssl/evp.h>
#include <zlib.h>

using json = nlohmann::json;

namespace {
	auto write_callback(char *ptr, size_t  size, size_t nmemb, void *userdata) -> size_t {
		auto *data = static_cast<std::string*>(userdata);
		data->append(ptr, nmemb);
		return nmemb;
	}
}

Game::Game(std::string previewUrl) : handle(curl_easy_init(), curl_easy_cleanup) {
	curl_error.reserve(CURL_ERROR_SIZE+1);
	curl_easy_setopt(&handle, CURLOPT_ERRORBUFFER, curl_error.data());
	CURLcode code = curl_easy_setopt(handle.get(), CURLOPT_URL, previewUrl.c_str());
	if (code != CURLE_OK) {
		std::cerr << "CURLOPT_URL failed: " << code << " " << curl_error << "\n";
	}
	code = curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
	if (code != CURLE_OK) {
		std::cerr << "CURLOPT_WRITEFUNCTION failed: " << code << " " << curl_error << "\n";
	}
}
void Game::update() {
	json game = fetch();

	new_turn = game["turns"] > turn_count;
	turn_count = game["turns"]; 
	if (new_turn) {
		std::cout << "New turn count: " << turn_count << "\n";
	}

	current_player = *std::ranges::find_if(game["civilizations"].begin(), game["civilizations"].end(), [&game](auto &civ) -> bool { return civ["civID"] == game["currentPlayer"];});
}
auto Game::isNewTurn() const -> bool {
	return new_turn;
}
auto Game::getCurrentPlayer() -> Civilization {
	return current_player;
}
auto Game::fetch() -> std::optional<json> {
	std::string data;
	CURLcode code = curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &data);
	if (code != CURLE_OK) {
		std::cerr << "curl_easy_setopt failed: " << code << " " << curl_error << "\n";
		return {};
	}
	code = curl_easy_perform(handle.get());
	if (code != CURLE_OK) {
		std::cerr << "curl_easy_perform failed: " << code << " " << curl_error << "\n";
		return {};
	}
	std::vector<unsigned char> data_vector(data.begin(), data.end());

	std::vector<unsigned char> inbuf(data.size()); // data always smaller than base64 
	int size = EVP_DecodeBlock(inbuf.data(), data_vector.data(), static_cast<int>(data_vector.size()));
	if (size == -1) {
		std::cerr << "Base64 decode failed: " << size << "\n";
	}
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
	if (ret != Z_STREAM_END && ret != Z_OK) {
		std::cerr << "Zlib decompression failed: " << ret << "\n";
		return {};
	}
	return json::parse(decompressed);
}
