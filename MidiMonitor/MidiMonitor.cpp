#include "stdafx.h"

namespace
{
	// MIDI message storage class
	class MidiMessage
	{
		uint32_t source_;
		uint8_t status_;
		uint8_t data1_;
		uint8_t data2_;

	public:

		MidiMessage(uint32_t source, uint32_t rawData)
			: source_(source), status_(rawData), data1_(rawData >> 8), data2_(rawData >> 16)
		{
		}

		uint64_t Encode64Bit()
		{
			uint64_t ul = source_;
			ul |= (uint64_t)status_ << 32;
			ul |= (uint64_t)data1_ << 40;
			ul |= (uint64_t)data2_ << 48;
			return ul;
		}

		std::string ToString()
		{
			char temp[256];
			std::snprintf(temp, sizeof(temp), "(%X) %02X %02X %02X", source_, status_, data1_, data2_);
			return temp;
		}
	};

	// Incoming MIDI message queue
	std::queue<MidiMessage> message_queue;

	// Device handler lists
	std::list<HMIDIIN> active_handlers;
	std::stack<HMIDIIN> handlers_to_close;

	// Mutex for resources
	std::recursive_mutex resource_lock;

	// MIDI input callback
	static void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
	{
		if (wMsg == MIM_DATA)
		{
			auto source_id = reinterpret_cast<uint32_t>(hMidiIn);
			resource_lock.lock();
			message_queue.push(MidiMessage(source_id, dwParam1));
			resource_lock.unlock();
		}
		else if (wMsg == MIM_CLOSE)
		{
			resource_lock.lock();
			handlers_to_close.push(hMidiIn);
			resource_lock.unlock();
		}
	}

	// Retrieve a name of a given device.
	std::wstring GetDeviceName(HMIDIIN handle)
	{
		auto casted_id = reinterpret_cast<UINT_PTR>(handle);
		MIDIINCAPS caps;
		if (midiInGetDevCaps(casted_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
			return caps.szPname;
		return L"unknown";
	}

	// Open a MIDI device with a given ID
	void OpenDevice(uint32_t id)
	{
		static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiInProc);
		HMIDIIN handle;
		if (midiInOpen(&handle, id, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
		{
			if (midiInStart(handle) == MMSYSERR_NOERROR)
			{
				resource_lock.lock();
				active_handlers.push_back(handle);
				resource_lock.unlock();

				std::wcout << L"Device opened: " << GetDeviceName(handle);
				std::wcout << L" at " << handle << std::endl;
			}
			else
			{
				midiInClose(handle);
			}
		}
	}

	// Close a given handler.
	void CloseDevice(HMIDIIN handle)
	{
		midiInClose(handle);

		resource_lock.lock();
		active_handlers.remove(handle);
		resource_lock.unlock();

		std::wcout << "Device closed: " << handle << std::endl;
	}

	// Open the all devices.
	void OpenAllDevices()
	{
		int device_count = midiInGetNumDevs();
		for (int i = 0; i < device_count; i++) OpenDevice(i);
	}

	// Refresh device handlers
	void RefreshDevices()
	{
		resource_lock.lock();

		// Close disconnected handlers.
		while (!handlers_to_close.empty()) {
			CloseDevice(handlers_to_close.top());
			handlers_to_close.pop();
		}

		// Try open all devices to detect newly connected ones.
		OpenAllDevices();

		resource_lock.unlock();
	}

	// Close the all devices.
	void CloseAllDevices()
	{
		resource_lock.lock();
		while (!active_handlers.empty())
			CloseDevice(active_handlers.front());
		resource_lock.unlock();
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	OpenAllDevices();

	while (true)
	{
		resource_lock.lock();

		while (!message_queue.empty())
		{
			auto text = message_queue.front().ToString();
			std::cout << text.c_str() << std::endl;
			message_queue.pop();
		}

		resource_lock.unlock();

		RefreshDevices();
		
		Sleep(100);
	}

	CloseAllDevices();

	return 0;
}

