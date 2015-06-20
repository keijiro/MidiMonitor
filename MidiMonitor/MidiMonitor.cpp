#include "stdafx.h"

namespace
{
	// Basic type aliases
	using DeviceHandle = HMIDIIN;
	using DeviceID = uint32_t;

#ifdef _WIN64

	DeviceID DeviceHandleToID(DeviceHandle handle)
	{
		return static_cast<DeviceID>(reinterpret_cast<uint64_t>(handle));
	}

	DeviceHandle DeviceIDToHandle(DeviceID id)
	{
		return reinterpret_cast<DeviceHandle>(static_cast<uint64_t>(id));
	}

#else

	DeviceID DeviceHandleToID(DeviceHandle handle)
	{
		return reinterpret_cast<DeviceID>(handle);
	}

	DeviceHandle DeviceIDToHandle(DeviceID id)
	{
		return reinterpret_cast<DeviceHandle>(id);
	}

#endif

	// MIDI message storage class
	class MidiMessage
	{
		DeviceID source_;
		uint8_t status_;
		uint8_t data1_;
		uint8_t data2_;

	public:

		MidiMessage(DeviceID source, uint32_t rawData)
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
	std::list<DeviceHandle> active_handles;
	std::stack<DeviceHandle> handles_to_close;

	// Mutex for resources
	std::recursive_mutex resource_lock;

	// MIDI input callback
	static void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
	{
		if (wMsg == MIM_DATA)
		{
			DeviceID id = DeviceHandleToID(hMidiIn);
			uint32_t raw = static_cast<uint32_t>(dwParam1);
			resource_lock.lock();
			message_queue.push(MidiMessage(id, raw));
			resource_lock.unlock();
		}
		else if (wMsg == MIM_CLOSE)
		{
			resource_lock.lock();
			handles_to_close.push(hMidiIn);
			resource_lock.unlock();
		}
	}

	// Retrieve a name of a given device.
	std::string GetDeviceName(DeviceHandle handle)
	{
		auto casted_id = reinterpret_cast<UINT_PTR>(handle);
		MIDIINCAPS caps;
		if (midiInGetDevCaps(casted_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			std::wstring name(caps.szPname);
			return std::string(name.begin(), name.end());
		}
		return "unknown";
	}

	// Open a MIDI device with a given index.
	void OpenDevice(unsigned int index)
	{
		static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiInProc);
		DeviceHandle handle;
		if (midiInOpen(&handle, index, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
		{
			if (midiInStart(handle) == MMSYSERR_NOERROR)
			{
				resource_lock.lock();
				active_handles.push_back(handle);
				resource_lock.unlock();

				std::cout << "Device opened: " << GetDeviceName(handle);
				std::cout << " at " << handle << std::endl;
			}
			else
			{
				midiInClose(handle);
			}
		}
	}

	// Close a given handler.
	void CloseDevice(DeviceHandle handle)
	{
		midiInClose(handle);

		resource_lock.lock();
		active_handles.remove(handle);
		resource_lock.unlock();

		std::cout << "Device closed: " << handle << std::endl;
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
		while (!handles_to_close.empty()) {
			CloseDevice(handles_to_close.top());
			handles_to_close.pop();
		}

		// Try open all devices to detect newly connected ones.
		OpenAllDevices();

		resource_lock.unlock();
	}

	// Close the all devices.
	void CloseAllDevices()
	{
		resource_lock.lock();
		while (!active_handles.empty())
			CloseDevice(active_handles.front());
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

