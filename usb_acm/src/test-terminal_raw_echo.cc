/*
 * \brief  Terminal echo that returns every byte unchanged
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * Unlike test/terminal_echo, no '\r' rewriting and no log line per read, so
 * binary test patterns come back as sent.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/component.h>
#include <terminal_session/connection.h>

namespace Test {

	using namespace Genode;

	struct Main;
}


struct Test::Main
{
	Env &_env;

	Terminal::Connection _terminal { _env };

	Signal_handler<Main> _read_avail { _env.ep(), *this, &Main::_handle_read_avail };

	char _buf[4096];

	void _handle_read_avail()
	{
		while (_terminal.avail()) {
			size_t const n = _terminal.read(_buf, sizeof(_buf));
			for (size_t written = 0; written < n; )
				written += _terminal.write(_buf + written, n - written);
		}
	}

	Main(Env &env) : _env(env)
	{
		_terminal.read_avail_sigh(_read_avail);
		_handle_read_avail();
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }
