#include <chrono>
#include <td/telegram/td_api.hpp>

class User {
	td::td_api::int53 chat_id;
	std::chrono::steady_clock::duration start_notify_interval;
	std::chrono::steady_clock::duration notify_interval;
	std::chrono::steady_clock::time_point last_notify;
	std::chrono::hours start_night;
	std::chrono::hours end_night;
	unsigned int night_messages = 0;
	unsigned int max_night_messages;
	unsigned int max_messages_per_turn;
	unsigned int messages_this_turn = 0;
	std::string notification;

	auto isNight(std::chrono::utc_clock::time_point time) -> bool;

	public:
	User(User &&) = delete;
	auto operator=(const User &) -> User & = default;
	auto operator=(User &&) -> User & = delete;
	~User() = default;
	User(td::td_api::int53 chat_id,
			std::chrono::steady_clock::duration start_notify_interval,
			unsigned int max_messages_per_turn,
			std::string notification, std::chrono::hours start_night,
			unsigned int max_night_messages, std::chrono::hours end_night);
	User(User const &) = delete;
	auto notifyIfNeeded(bool new_turn)
		-> std::optional<td::td_api::object_ptr<td::td_api::sendMessage>>;
	auto getNotifyInterval() -> std::chrono::steady_clock::duration;
	[[nodiscard]] auto getChatID() const -> td::td_api::int53;
};
