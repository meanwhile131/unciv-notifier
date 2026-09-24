#include <chrono>
#include <td/telegram/td_api.hpp>

class User {
	td::td_api::int53 chat_id;
	std::string uuid;
	std::chrono::steady_clock::duration start_notify_interval;
	std::chrono::steady_clock::duration notify_interval = start_notify_interval;
	std::chrono::steady_clock::time_point last_notify;
	std::chrono::hours start_night;
	std::chrono::hours end_night;
	unsigned int night_messages = 0;
	unsigned int max_night_messages;
	std::string notification;
	auto isNight(std::chrono::steady_clock::time_point time) -> bool;

	public:
	User(td::td_api::int53 chat_id, std::string uuid, std::chrono::steady_clock::duration start_notify_interval, std::string notification, std::chrono::hours start_night, unsigned int max_night_messages, std::chrono::hours end_night);
	auto notifyIfNeeded(bool new_turn) -> std::optional<td::td_api::object_ptr<td::td_api::sendMessage>>;
	auto getNotifyInterval() -> std::chrono::steady_clock::duration;
	auto getUUID() -> std::string;
	[[nodiscard]] auto getChatID() const -> td::td_api::int53;
};
