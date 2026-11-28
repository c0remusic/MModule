#include "edDrumsPushLink.h"

#include <cmath>
#include <cstring>

namespace elektron::drums
{
	namespace osc
	{
		namespace
		{
			void putString(std::vector<uint8_t>& _out, const std::string& _s)
			{
				_out.insert(_out.end(), _s.begin(), _s.end());
				// At least one NUL, then to a multiple of four
				do
					_out.push_back(0);
				while(_out.size() % 4);
			}

			// A padded string at _at, which moves past it; false past the end or unterminated
			bool getString(const uint8_t* _data, const size_t _size, size_t& _at, std::string& _s)
			{
				const auto* begin = _data + _at;
				const auto* end = static_cast<const uint8_t*>(std::memchr(begin, 0, _size - _at));
				if(!end)
					return false;
				_s.assign(reinterpret_cast<const char*>(begin), static_cast<size_t>(end - begin));
				_at += (_s.size() + 4) & ~size_t{3};
				return _at <= _size;
			}

			uint32_t getWord(const uint8_t* _p)
			{
				return static_cast<uint32_t>(_p[0]) << 24 | static_cast<uint32_t>(_p[1]) << 16
					| static_cast<uint32_t>(_p[2]) << 8 | _p[3];
			}
		}

		std::vector<uint8_t> encode(const Message& _message)
		{
			std::vector<uint8_t> out;
			putString(out, _message.address);
			putString(out, "," + std::string(_message.args.size(), 'i'));
			for(const int v : _message.args)
			{
				const auto u = static_cast<uint32_t>(v);
				out.insert(out.end(), {static_cast<uint8_t>(u >> 24), static_cast<uint8_t>(u >> 16),
					static_cast<uint8_t>(u >> 8), static_cast<uint8_t>(u)});
			}
			return out;
		}

		std::optional<Message> decode(const uint8_t* _data, const size_t _size)
		{
			if(_size % 4 || _size < 4 || _data[0] != '/')
				return std::nullopt;
			Message m;
			size_t at = 0;
			std::string tags;
			if(!getString(_data, _size, at, m.address))
				return std::nullopt;
			// A message without its type tags carries no argument
			if(at == _size)
				return m;
			if(!getString(_data, _size, at, tags) || tags.empty() || tags[0] != ',')
				return std::nullopt;
			for(size_t t = 1; t < tags.size(); ++t)
			{
				if((tags[t] != 'i' && tags[t] != 'f') || at + 4 > _size)
					return std::nullopt;
				const uint32_t w = getWord(_data + at);
				at += 4;
				if(tags[t] == 'i')
				{
					m.args.push_back(static_cast<int32_t>(w));
					continue;
				}
				float f;
				std::memcpy(&f, &w, 4);
				if(!std::isfinite(f) || std::abs(f) > 2e9f)
					return std::nullopt;
				m.args.push_back(static_cast<int>(std::lround(f)));
			}
			return m;
		}
	}

	std::array<int, 4> PushHello::velocities(const int _port)
	{
		return {HelloMark, 1 + (_port >> 12 & 63), 1 + (_port >> 6 & 63), 1 + (_port & 63)};
	}

	int PushHello::note(const int _note, const int _velocity)
	{
		const int i = _note - FirstNote;
		if(i == 0)
		{
			m_have = _velocity == HelloMark ? 1 : 0;
			m_port = 0;
			return 0;
		}
		// Out of order, or a velocity no hello gives: the hello is dropped
		if(i < 1 || i > 3 || m_have != i || _velocity < 1 || _velocity > 64)
		{
			m_have = 0;
			return 0;
		}
		m_port = m_port << 6 | (_velocity - 1);
		if(++m_have < 4)
			return 0;
		m_have = 0;
		return m_port > 0 && m_port < 65536 ? m_port : 0;
	}

	PushLink::PushLink() : juce::Thread("Lockstep Push link")
	{
		// Local only: nothing off this computer reaches it
		if(!m_socket.bindToPort(0, "127.0.0.1"))
			return;
		m_port = m_socket.getBoundPort();
		startThread();
	}

	PushLink::~PushLink()
	{
		signalThreadShouldExit();
		m_socket.shutdown();
		stopThread(2000);
	}

	bool PushLink::send(const osc::Message& _message)
	{
		if(!m_port || m_companion <= 0)
			return false;
		const auto data = osc::encode(_message);
		return m_socket.write("127.0.0.1", m_companion, data.data(), static_cast<int>(data.size()))
			== static_cast<int>(data.size());
	}

	std::vector<osc::Message> PushLink::take()
	{
		const std::lock_guard lock(m_mutex);
		return std::exchange(m_received, {});
	}

	void PushLink::run()
	{
		std::array<uint8_t, 2048> buffer{};
		while(!threadShouldExit())
		{
			if(m_socket.waitUntilReady(true, 100) != 1)
				continue;
			juce::String from;
			int fromPort = 0;
			const int n = m_socket.read(buffer.data(), static_cast<int>(buffer.size()), false, from, fromPort);
			if(n <= 0)
				continue;
			auto m = osc::decode(buffer.data(), static_cast<size_t>(n));
			if(!m)
				continue;
			const std::lock_guard lock(m_mutex);
			// A companion that stops reading must not grow this without end
			if(m_received.size() < 4096)
				m_received.push_back(std::move(*m));
		}
	}
}
