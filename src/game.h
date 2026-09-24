#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "user.h"

namespace td_api = td::td_api;

struct Civilization {
	std::string playerId;
	std::string civID;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Civilization, playerId, civID);

class Game {
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle;
	unsigned int turn_count{};
	bool new_turn = false;
	Civilization current_player;
	std::vector<User> users;

	auto fetch() -> nlohmann::json;

	public:
	Game(const std::string& previewUrl, const std::vector<User>& users);
	void update();
	auto getNotifications() -> std::vector<td_api::object_ptr<td_api::sendMessage>>;
	[[nodiscard]] auto isNewTurn() const -> bool;
        auto getCurrentPlayer() -> Civilization;
};
