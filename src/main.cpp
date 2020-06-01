#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <sdbus-c++/sdbus-c++.h>

/*
dbus-send --print-reply --system --type=method_call --dest=cz.cesnet.led /cz/cesnet/led cz.cesnet.Led.SetState string:green
dbus-send --print-reply --system --type=method_call --dest=cz.cesnet.led /cz/cesnet/led cz.cesnet.Led.GetState
*/

/* get systemd-NFailedUints  value via dbus-send
dbus-send --system --print-reply  --dest=org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.DBus.Properties.Get string:org.freedesktop.systemd1.Manager string:NFailedUnits
*/

std::string serviceName = "cz.cesnet.led";
std::string objectPath = "/cz/cesnet/led";
std::string interface = "cz.cesnet.Led";
std::string error = "cz.cesnet.Led.Error";

enum class State : uint32_t {
	GREEN, ORANGE, RED,
};

// --------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const State& o) {
	switch(o) {
		case State::RED: return os << "(State red)";
		case State::GREEN: return os << "(State green)";
		case State::ORANGE: return os << "(State orange)";
	}

	return os << "(State unknown)";
}

std::ostream& operator<<(std::ostream& os, const std::vector<std::string>& obj) {
	os << "[";
	for (const auto& k : obj)
		os << k << ", ";
	return os << "]";
}

std::ostream& operator<<(std::ostream& os, const sdbus::Variant& obj) {
	os << "(Variant ";

	if (obj.peekValueType() == "u") {
		os << "int " << std::to_string(obj.get< uint32_t >());
	} else {
		os << "unknown";
	}

	return os << ")";
}

std::ostream& operator<<(std::ostream& os, const std::map<std::string, sdbus::Variant>& obj) {
	os << "{";
	for (const auto& [k,v] : obj)
		os << k << ": " << v << ", ";
	return os << "}";
}

// --------------------------------------------------------------------------

// Observer?
class LedDriver {
private:
	State m_state;

public:
	State getState() const {
		return m_state;
	}
	void color(const State state) {
		m_state = state;
		std::cout << "led: Blink! " << state << std::endl;
	}
};

// --------------------------------------------------------------------------

class Mux;
class MuxInput {
	State m_state;
	std::shared_ptr<Mux> m_mx;

public:
	MuxInput(std::shared_ptr<Mux> mx)
	: m_state(State::RED)
	, m_mx(mx) {
	}
	State getValue() const;
	void setState(const State state);
};

// Observable?
class Mux {
	std::vector<std::shared_ptr<MuxInput>> m_inputs;
	LedDriver ld;

public:
	Mux() {
		ld.color(State::RED);
	}
	void notify() {
		std::cout << "mux: Notified of change!";

		auto it = std::max_element(m_inputs.begin(), m_inputs.end(), [](const auto& a, const auto& b) { return a->getValue() < b->getValue(); });
		if (it != m_inputs.end()) {
			std::cout << " Computed new state: [";
			for (const auto & e : m_inputs) std::cout << e -> getValue() << ", ";
			std::cout << "] " << (*it)->getValue() << std::endl;

			ld.color((*it)->getValue());
		}
		else
			std::cout << std::endl;

	}

	void registerInput(std::shared_ptr<MuxInput> inp) {
		m_inputs.push_back(inp);
	}
};

State MuxInput::getValue() const { return m_state; }
void MuxInput::setState(const State state) {
	m_state = state;
	m_mx -> notify();
}

// --------------------------------------------------------------------------

class External : public MuxInput {
	std::shared_ptr<sdbus::IObject> m_dbusStateObj;

public:
	External (sdbus::IConnection& connection, std::shared_ptr<Mux> mx)
	: MuxInput (mx)
	, m_dbusStateObj(sdbus::createObject(connection, objectPath)) // Create a D-Bus object.
	{
		setState(State::RED);

		// Register D-Bus methods and signals on the object, and exports the object.
		m_dbusStateObj->registerMethod("SetState").onInterface(interface).implementedAs(
				[&](std::string state){this->DbusMethodSetState(state); }
		);
		m_dbusStateObj->registerMethod("GetState").onInterface(interface).implementedAs(
				[&]() {
					std::ostringstream oss;
					oss << getValue();
					return oss.str();
				}
		);
		m_dbusStateObj->finishRegistration();
	}

	void DbusMethodSetState(const std::string &state) {
		std::cout << "ext: " << interface << ": SetState(" << state << ")" << std::endl;

		State state2;

		if (state == "red")
			state2 = State::RED;
		else if (state == "green")
			state2 = State::GREEN;
		else if (state == "orange")
			state2 = State::ORANGE;
		else {
			std::cout << "Invalid state selected - raising dbus exception" << std::endl;
			throw sdbus::Error(error, "asd");
		}

		setState(state2);
	}
};

// --------------------------------------------------------------------------

class Systemd : public MuxInput {
	std::unique_ptr<sdbus::IProxy> m_systemdProxy;

public:
	Systemd (sdbus::IConnection& connection, std::shared_ptr<Mux> mx)
	: MuxInput (mx)
	, m_systemdProxy(sdbus::createProxy(connection, "org.freedesktop.systemd1", "/org/freedesktop/systemd1"))
	{
		m_systemdProxy->uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties").call(
				[&](const std::string& iface, const std::map<std::string, sdbus::Variant>& changed, const std::vector<std::string>& invalidated) {
					std::cout << "sys: " << iface << ": Changed: " << changed << ", invalidated: " << invalidated << std::endl;

					if (changed.count("NFailedUnits")) {
						setState(changed.at("NFailedUnits").get<uint32_t>() > 0 ? State::RED : State::GREEN);
					}
				}
		);
		m_systemdProxy->finishRegistration();

		// TODO: possible race?
		uint32_t failedUnits = m_systemdProxy->getProperty("NFailedUnits").onInterface("org.freedesktop.systemd1.Manager");
		setState(failedUnits > 0 ? State::RED : State::GREEN);
	}
};

// --------------------------------------------------------------------------

int main()
{
    // Create D-Bus connection to the system bus and requests name on it.
    // This daemon "owns" this prefix - reusable within this process
    std::unique_ptr<sdbus::IConnection> connection1 = sdbus::createSystemBusConnection(serviceName);

	std::shared_ptr<Mux> mx = std::make_shared<Mux>();
	mx -> registerInput(std::make_shared<External>(*connection1.get(), mx)); // TODO: Shared or separate connections?
	mx -> registerInput(std::make_shared<Systemd>(*connection1.get(), mx));

	connection1->enterEventLoop();
}
