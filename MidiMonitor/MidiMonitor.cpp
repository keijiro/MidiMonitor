#include "stdafx.h"

namespace
{
	// MIDI message storage class
	class MidiMessage
	{
		uint32_t source_id_;
		uint8_t status_;
		uint8_t data1_;
		uint8_t data2_;

	public:

		MidiMessage(uint32_t source_id, uint32_t rawData)
			: source_id_(source_id), status_(rawData), data1_(rawData >> 8), data2_(rawData >> 16)
		{
		}

		uint64_t Encode64Bit()
		{
			return source_id_ || ((uint64_t)status_ << 32) || ((uint64_t)data1_ << 40) || ((uint64_t)data2_ << 48);
		}

		void Print() const
		{
			std::printf("(%08X) %02X %02X %02X\n", source_id_, status_, data1_, data2_);
		}
	};

	// Incoming MIDI message queue
	std::queue<MidiMessage> message_queue;

	// Device handler lists
	std::list<HMIDIIN> active_handlers;
	std::stack<HMIDIIN> handlers_to_close;

	// Mutex for resources
	std::mutex resource_lock;

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
			std::printf("Device (%08X) disconnected", hMidiIn);
			resource_lock.lock();
			handlers_to_close.push(hMidiIn);
			resource_lock.unlock();
		}
	}

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
			}
			else
				midiInClose(handle);
		}
	}

	void OpenAllDevices()
	{
		int device_count = midiInGetNumDevs();
		for (int i = 0; i < device_count; i++) OpenDevice(i);
	}

	void RefreshDevices()
	{
		resource_lock.lock();

		while (!handlers_to_close.empty()) {
			midiInClose(handlers_to_close.top());
			active_handlers.remove(handlers_to_close.top());
			handlers_to_close.pop();
		}

		std::set<uint32_t> active_ids;

		for (auto& h : active_handlers) {
			uint32_t id;
			if (midiInGetID(h, &id) == MMSYSERR_NOERROR) {
				active_ids.insert(id);
			}
		}

		resource_lock.unlock();

		int device_count = midiInGetNumDevs();
		for (int i = 0; i < device_count; i++)
		{
			if (active_ids.count(i) == 0) OpenDevice(i);
		}
	}

	void CloseAllDevices()
	{
		for (auto& h : active_handlers)
			midiInClose(h);
		active_handlers.clear();
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
			message_queue.front().Print();
			message_queue.pop();
		}

		resource_lock.unlock();

		RefreshDevices();
		
		Sleep(100);
	}

	CloseAllDevices();

	return 0;
}

