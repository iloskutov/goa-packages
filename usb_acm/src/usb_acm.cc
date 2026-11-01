/*
 * \brief  USB CDC ACM function: a serial port to the USB host
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * Client of the gadget driver's Usb_gadget session, server of one Terminal
 * session. Bytes from the host become terminal input, terminal output goes
 * to the host. While the host has not configured the device or not opened
 * the port (DTR low), output is dropped, so a writer never blocks on a
 * missing host. Endpoints (acm.h): 1 notification (unused), 2 bulk IN,
 * 3 bulk OUT; OUT buffers of one max packet of the enabled speed.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/allocator_avl.h>
#include <base/attached_ram_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <root/component.h>
#include <terminal_session/terminal_session.h>
#include <usb_gadget/acm.h>
#include <usb_gadget_session/connection.h>

namespace Usb_acm {

	using namespace Genode;

	struct Rx_buffer;
	struct Function;
	struct Session_component;
	struct Root;
	struct Main;

	using Packet = Usb_gadget::Packet_descriptor;
}


/**
 * Bytes received from the host, not yet read by the terminal client
 */
struct Usb_acm::Rx_buffer
{
	enum { SIZE = 64*1024 };

	char   _data[SIZE];
	size_t _head = 0, _count = 0;

	size_t avail() const { return _count; }
	size_t space() const { return SIZE - _count; }

	void reset() { _head = _count = 0; }

	void put(char const *src, size_t n)
	{
		for (size_t i = 0; i < n && _count < SIZE; i++, _count++)
			_data[(_head + _count) % SIZE] = src[i];
	}

	size_t get(char *dst, size_t n)
	{
		size_t i = 0;
		for (; i < n && _count; i++, _count--, _head = (_head + 1) % SIZE)
			dst[i] = _data[_head];
		return i;
	}
};


struct Usb_acm::Function
{
	enum { EP_IN = 2, EP_OUT = 3, OUT_PACKETS = 16, IN_CHUNK = 4096,
	       EP0_AREA = 128 };

	Env &_env;

	Heap          _heap     { _env.ram(), _env.rm() };
	Allocator_avl _tx_alloc { &_heap };

	Usb_gadget::Connection _gadget { _env, _tx_alloc, "acm" };

	Io_signal_handler<Function> _io_handler { _env.ep(), *this, &Function::_handle_io };

	Signal_context_capability _read_avail_sigh { };

	Usb_gadget::Acm   _acm { };
	Usb_gadget::Setup _setup { };   /* request waiting for its data stage */

	/*
	 * Buffer of the control answers, allocated once: data and OUT buffers
	 * may fill the packet buffer and all but one submit slot, the host
	 * still gets its answer (the slot is kept free by '_fill_out' and
	 * 'write')
	 */
	Packet _ep0_area { };

	Packet _ep0(size_t len)
	{
		Packet p(_ep0_area.offset(), min(len, (size_t)EP0_AREA));
		p.endpoint = 0;
		return p;
	}

	bool     _enabled     = false;
	bool     _dropping    = false;
	unsigned _out_pending = 0;

	/*
	 * OUT buffers of one max packet: the host (cdc_acm) sends no
	 * zero-length packet, so a larger buffer would not complete after a
	 * transfer of max-packet multiples (as u_serial does it)
	 */
	size_t _out_size = 512;

	static size_t _max_packet(Usb_gadget::Event::Speed speed)
	{
		switch (speed) {
		case Usb_gadget::Event::FULL: return 64;
		case Usb_gadget::Event::HIGH: return 512;
		default:                      return 1024;
		}
	}

	Rx_buffer _rx { };

	bool _port_open() const { return _enabled && _acm.dtr; }

	void _submit(Packet p) { _gadget.tx()->submit_packet(p); }

	Packet _alloc(size_t size, uint8_t ep)
	{
		Packet p = _gadget.alloc_packet(size, ep);
		if (size && p.size() != size)
			error("out of packet buffer for ", size, " bytes");
		return p;
	}

	/**
	 * Keep OUT buffers queued, as long as the received data fits
	 */
	void _fill_out()
	{
		while (_enabled && _out_pending < OUT_PACKETS
		    && _rx.space() >= (_out_pending + 1) * _out_size
		    && _gadget.tx()->ready_to_submit(2)) {
			Packet const p = _alloc(_out_size, EP_OUT);
			if (!p.size()) return;
			_submit(p);
			_out_pending++;
		}
	}

	void _ep0_reply(Usb_gadget::Reply reply, unsigned char const *in, size_t in_len)
	{
		switch (reply) {
		case Usb_gadget::Reply::DATA_IN:
		{
			Packet p = _ep0(in_len);
			char *d = _gadget.tx()->packet_content(p);
			for (size_t i = 0; i < p.size(); i++) d[i] = (char)in[i];
			_submit(p);
			return;
		}
		case Usb_gadget::Reply::DATA_OUT:
			_submit(_ep0(_setup.length));
			return;
		case Usb_gadget::Reply::STATUS:
			_submit(_ep0(0));
			return;
		case Usb_gadget::Reply::STALL:
		{
			Packet p = _ep0(0);
			p.stall = true;
			_submit(p);
			return;
		}
		}
	}

	void _handle_setup(uint8_t const *s)
	{
		_setup = { s[0], s[1], uint16_t(s[2] | s[3] << 8),
		           uint16_t(s[4] | s[5] << 8), uint16_t(s[6] | s[7] << 8) };

		bool const was_open = _port_open();

		unsigned char in[8] { };
		unsigned long in_len = 0;
		_ep0_reply(Usb_gadget::handle(_acm, _setup, nullptr, in, in_len), in, in_len);

		if (!was_open && _port_open()) {
			log("port opened by the host");
			_dropping = false;
		}
		if (was_open && !_port_open())
			log("port closed by the host");
	}

	void _handle_ep0_ack(Packet const &p)
	{
		bool const data_stage = p.status == Packet::OK && p.size()
		                     && !(_setup.type & 0x80) && _setup.length == p.size();
		if (!data_stage) return;

		unsigned char in[8] { };
		unsigned long in_len = 0;
		unsigned char const *data = (unsigned char const *)_gadget.tx()->packet_content(p);
		_ep0_reply(Usb_gadget::handle(_acm, _setup, data, in, in_len), in, in_len);
	}

	void _handle_acks()
	{
		auto &source = *_gadget.tx();
		bool received = false;

		while (source.ack_avail()) {
			Packet const p = source.get_acked_packet();

			if (p.endpoint == EP_OUT) {
				if (_out_pending) _out_pending--;
				if (p.status == Packet::OK && p.actual) {
					_rx.put(source.packet_content(p), p.actual);
					received = true;
				}
			}
			/* the ep0 area stays allocated */
			if (p.endpoint == 0)
				_handle_ep0_ack(p);
			else
				source.release_packet(p);
		}

		_fill_out();

		if (received && _read_avail_sigh.valid())
			Signal_transmitter(_read_avail_sigh).submit();
	}

	void _handle_events()
	{
		for (;;) {
			Usb_gadget::Event const e = _gadget.event();

			switch (e.type) {
			case Usb_gadget::Event::NONE:
				return;
			case Usb_gadget::Event::ENABLED:
				_out_size = _max_packet(e.speed);
				log("enabled by the host, OUT buffers of ", _out_size, " bytes");
				_enabled = true;
				_fill_out();
				break;
			case Usb_gadget::Event::DISABLED:
				log("disabled");
				/* OUT buffers come back FLUSHED, '_out_pending' follows */
				_enabled = false;
				_acm.dtr = false;
				break;
			case Usb_gadget::Event::SETUP:
				_handle_setup(e.setup);
				break;
			default:
				break;
			}
		}
	}

	void _handle_io()
	{
		/* acknowledgements queued before an event are seen first */
		_handle_acks();
		_handle_events();
	}

	Function(Env &env) : _env(env)
	{
		using Blob = Usb_gadget::Session::Blob;
		auto const blob = [] (auto const &a) { return Blob((char const *)&a, sizeof(a)); };

		_ep0_area = _gadget.alloc_packet(EP0_AREA, 0);

		_gadget.event_sigh(_io_handler);
		_gadget.tx_channel()->sigh_ack_avail(_io_handler);
		_gadget.tx_channel()->sigh_ready_to_submit(_io_handler);

		if (!_gadget.function(blob(Usb_gadget::acm_fs), blob(Usb_gadget::acm_hs),
		                      blob(Usb_gadget::acm_ss),
		                      Blob(Usb_gadget::acm_strings, sizeof(Usb_gadget::acm_strings))))
			error("gadget driver rejected the ACM descriptors");
	}

	void read_avail_sigh(Signal_context_capability sigh) { _read_avail_sigh = sigh; }

	bool avail() const { return _rx.avail(); }

	size_t read(char *dst, size_t n)
	{
		size_t const got = _rx.get(dst, n);
		_fill_out();
		return got;
	}

	/**
	 * Send 'n' bytes to the host, or drop them if the port is not open
	 */
	void write(char const *src, size_t n)
	{
		while (n) {
			if (!_port_open()) {
				if (!_dropping) log("port not open, dropping output");
				_dropping = true;
				return;
			}

			size_t const chunk = min(n, (size_t)IN_CHUNK);
			Packet p = _gadget.alloc_packet(chunk, EP_IN);
			if (p.size() != chunk || !_gadget.tx()->ready_to_submit(2)) {
				if (p.size()) _gadget.tx()->release_packet(p);
				_env.ep().wait_and_dispatch_one_io_signal();
				continue;
			}

			char *d = _gadget.tx()->packet_content(p);
			for (size_t i = 0; i < chunk; i++) d[i] = src[i];
			p.zlp = true;
			_submit(p);

			src += chunk;
			n   -= chunk;
		}
	}
};


struct Usb_acm::Session_component : Rpc_object<Terminal::Session, Session_component>
{
	Function &_function;

	Attached_ram_dataspace _io_buffer;

	Session_component(Env &env, Function &function)
	:
		_function(function), _io_buffer(env.ram(), env.rm(), 4096)
	{ }

	Size size() override { return Size(0, 0); }

	bool avail() override { return _function.avail(); }

	size_t _read(size_t n)
	{
		return _function.read(_io_buffer.local_addr<char>(), min(n, _io_buffer.size()));
	}

	size_t _write(size_t n)
	{
		n = min(n, _io_buffer.size());
		_function.write(_io_buffer.local_addr<char const>(), n);
		return n;
	}

	Dataspace_capability _dataspace() { return _io_buffer.cap(); }

	void read_avail_sigh(Signal_context_capability sigh) override {
		_function.read_avail_sigh(sigh); }

	void size_changed_sigh(Signal_context_capability) override { }

	void connected_sigh(Signal_context_capability sigh) override {
		Signal_transmitter(sigh).submit(); }

	size_t read(void *, size_t) override { return 0; }
	size_t write(void const *, size_t) override { return 0; }
};


struct Usb_acm::Root : Root_component<Session_component, Genode::Single_client>
{
	Env      &_env;
	Function &_function;

	Create_result _create_session(char const *) override {
		return *new (md_alloc()) Session_component(_env, _function); }

	Root(Env &env, Allocator &md_alloc, Function &function)
	:
		Root_component<Session_component, Genode::Single_client>(&env.ep().rpc_ep(), &md_alloc),
		_env(env), _function(function)
	{ }
};


struct Usb_acm::Main
{
	Env &_env;

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

	Function _function { _env };

	Root _root { _env, _sliced_heap, _function };

	Main(Env &env) : _env(env) { env.parent().announce(env.ep().manage(_root)); }
};


void Component::construct(Genode::Env &env) { static Usb_acm::Main main(env); }
