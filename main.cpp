#include <iostream>
#include <memory>
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include "overload.h"

namespace td_api = td::td_api;

class UncivNotifier {
	public:
		std::unique_ptr<td::ClientManager> client_manager;
		td::ClientManager::ClientId client_id;
		UncivNotifier() {
			td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
			client_manager = std::make_unique<td::ClientManager>();
			client_id = client_manager->create_client_id();
			client_manager->send(client_id, 1, td_api::make_object<td_api::getOption>("version"));
			auto proxy = td_api::make_object<td_api::proxy>(
					"nya-nya.top",
					853,
					td_api::make_object<td_api::proxyTypeMtproto>("7lTOMw5GkMwpfSsDH_PyiLBtdC5ha2VuYWkuY2xpY2s"));
			auto request = td_api::make_object<td_api::addProxy>(std::move(proxy), true, "");
			client_manager->send(client_id, 1, std::move(request));
		}
		void loop() {
			while (true) {
				auto update = client_manager->receive(300);
				td_api::downcast_call(*update.object, overloaded(
							[this](td_api::updateAuthorizationState &state) {
							td_api::downcast_call(*state.authorization_state_, overloaded(
										[this](td_api::authorizationStateWaitTdlibParameters &) {
										auto request = td_api::make_object<td_api::setTdlibParameters>();
										request->api_id_ = 94575;
										request->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
										request->system_language_code_ = "en";
										request->device_model_ = "Desktop";
										request->application_version_ = "1.0";
										client_manager->send(client_id, 1, std::move(request));
										},
										[this](td_api::authorizationStateWaitPhoneNumber &) {
										auto request = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
										std::string phone;
										std::cout << "Phone number: " << std::flush;
										std::getline(std::cin, phone);
										request->phone_number_ = phone;
										client_manager->send(client_id, 1, std::move(request));
										},
										[this](td_api::authorizationStateWaitCode &) {
										auto request = td_api::make_object<td_api::checkAuthenticationCode>();
										std::string code;
										std::cout << "Code: " << std::flush;
										std::getline(std::cin, code);
										request->code_ = code;
										client_manager->send(client_id, 1, std::move(request));
										},
										[](auto &upd) {
										std::cout << td_api::to_string(upd) << std::endl;
										}));
							},
							[](auto &upd) {
							std::cout << td_api::to_string(upd) << std::endl;
							}));
			}
		}
};

int main() {
	UncivNotifier app;
	app.loop();
}
