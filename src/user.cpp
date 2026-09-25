#include "user.hpp"
#include <iostream>
#include <utility>

namespace td_api = td::td_api;
User::User(td::td_api::int53 chat_id, std::chrono::steady_clock::duration start_notify_interval, unsigned int max_messages_per_turn, std::string notification, std::chrono::hours start_night, unsigned int max_night_messages, std::chrono::hours end_night) : chat_id(chat_id), max_messages_per_turn(max_messages_per_turn), start_notify_interval(start_notify_interval), notify_interval(start_notify_interval), notification(std::move(notification)), start_night(start_night), end_night(end_night), max_night_messages(max_night_messages) {}

auto User::notifyIfNeeded(bool new_turn) -> std::optional<td_api::object_ptr<td_api::sendMessage>> {
	if (new_turn) {
		notify_interval = start_notify_interval;
		messages_this_turn = 0;
	}
	if (messages_this_turn >= max_messages_per_turn) {
		std::cout << "Skipping game checks because max message count per turn reached" << '\n';
		return {};
	}
	auto now = std::chrono::steady_clock::now();
	std::cout << "last notify: " << std::chrono::duration_cast<std::chrono::seconds>(last_notify.time_since_epoch()) << "\n";
	auto since_last_notify = now - last_notify;
	std::cout << "Since last notify: " << std::chrono::duration_cast<std::chrono::seconds>(since_last_notify) << "\n";
	bool is_night = isNight(std::chrono::utc_clock::now());
	if (is_night && night_messages >= max_night_messages) {
		std::cout << "Skipping game checks because night and max message count reached" << '\n';
		return {};
	}
	if (!is_night && night_messages != 0) {
		std::cout << "Resetting night message count" << '\n';
		night_messages = 0;
	}
	bool should_notify = (since_last_notify >= notify_interval) || new_turn;
	if (!should_notify) {
		return {};
	}
	auto request = td_api::make_object<td_api::sendMessage>();
	request->chat_id_ = chat_id;
	auto message = td_api::make_object<td_api::inputMessageText>();
	auto text = td_api::make_object<td_api::formattedText>();
	text->text_ = notification;
	message->text_ = std::move(text);
	request->input_message_content_ = std::move(message);
	notify_interval *= 2;
	last_notify = now;
	messages_this_turn++;
	std::cout << "Used " << messages_this_turn << "/" << max_messages_per_turn << " messages per turn" << '\n';
	std::cout << "Updated last notify to " << std::chrono::duration_cast<std::chrono::seconds>(last_notify.time_since_epoch()) << "\n";
	if (is_night) {
		night_messages++;
		std::cout << "Used " << night_messages << "/" << max_night_messages << " night messages" << '\n';
	}
	return request;
}
auto User::isNight(std::chrono::utc_clock::time_point time) -> bool {
	auto days = std::chrono::floor<std::chrono::days>(time);
	std::chrono::hh_mm_ss time_of_day(time - days);
	auto hours = time_of_day.hours();
	bool overnight = start_night > end_night;
	bool is_night_overnight = hours >= start_night || hours <= end_night;
	bool is_night_day = hours >= start_night && hours <= end_night;
	return overnight ? is_night_overnight : is_night_day;
}
auto User::getNotifyInterval() -> std::chrono::steady_clock::duration {
	return notify_interval;
}
auto User::getChatID() const -> td::td_api::int53 {
	return chat_id;
}
