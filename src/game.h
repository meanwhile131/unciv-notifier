#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "user.h"

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
	std::string curl_error;

	auto fetch() -> std::optional<nlohmann::json>;

	public:
	explicit Game(std::string previewUrl);
	void update();
	[[nodiscard]] auto isNewTurn() const -> bool;
        auto getCurrentPlayer() -> Civilization;
};
