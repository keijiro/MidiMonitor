#include <CoreMIDI/CoreMIDI.h>
#include <iostream>
#include <queue>
#include <mutex>
#include <vector>

#pragma mark Private classes

namespace
{
    // MIDI message storage class
    class MidiMessage
    {
        MIDIUniqueID source_;
        uint8_t status_;
        uint8_t data_[2];
        
    public:

        MidiMessage(MIDIUniqueID source, uint8_t status)
            : source_(source), status_(status)
        {
            data_[0] = data_[1] = 0;
        }
        
        void SetData(int offs, uint8_t byte)
        {
            if (offs < 2) data_[offs] = byte;
        }

        uint64_t Encode64Bit() const
        {
            uint64_t ul = source_;
            ul |= (uint64_t)status_ << 32;
            ul |= (uint64_t)data_[0] << 40;
            ul |= (uint64_t)data_[1] << 48;
            return ul;
        }

        std::string ToString() const
        {
            char temp[256];
            std::snprintf(temp, sizeof(temp), "(%X) %02X %02X %02X", source_, status_, data_[0], data_[1]);
            return temp;
        }
    };
    
    // MIDI source ID array
    std::vector<MIDIUniqueID> source_ids;
    
    // Incoming MIDI message queue
    std::queue<MidiMessage> message_queue;
    std::mutex message_queue_lock;
    
    // Core MIDI objects
    MIDIClientRef midi_client;
    MIDIPortRef midi_port;
    
    // Reset-is-required flag
    bool reset_required = true;
}

#pragma mark Core MIDI callbacks

namespace
{
    extern "C" void MIDIStateChangedHander(const MIDINotification* message, void* refCon)
    {
        // Reset if somthing has changed.
        if (message->messageID == kMIDIMsgSetupChanged) reset_required = true;
    }
    
    extern "C" void MIDIReadProc(const MIDIPacketList *packetList, void *readProcRefCon, void *srcConnRefCon)
    {
        auto source_id = *reinterpret_cast<MIDIUniqueID*>(srcConnRefCon);
        
        message_queue_lock.lock();
        
        // Transform the packets into MIDI messages and push it to the message queue.
        const MIDIPacket *packet = &packetList->packet[0];
        for (int packetCount = 0; packetCount < packetList->numPackets; packetCount++) {
            // Extract MIDI messages from the data stream.
            for (int offs = 0; offs < packet->length;) {
                MidiMessage message(source_id, packet->data[offs++]);
                for (int dc = 0; offs < packet->length && (packet->data[offs] < 0x80); dc++, offs++)
                    message.SetData(dc, packet->data[offs]);
                message_queue.push(message);
            }
            packet = MIDIPacketNext(packet);
        }
        
        message_queue_lock.unlock();
    }
}

#pragma mark Private functions

namespace
{
    // Reset the status if required.
    // Returns false when something goes wrong.
    bool ResetIfRequired()
    {
        if (!reset_required) return true;
        
        // Dispose the old MIDI client if exists.
        if (midi_client != 0) MIDIClientDispose(midi_client);
        
        // Create a MIDI client.
        if (MIDIClientCreate(CFSTR("UnityMIDIReceiver Client"), MIDIStateChangedHander, nullptr, &midi_client) != noErr) return false;
        
        // Create a MIDI port which covers all the MIDI sources.
        if (MIDIInputPortCreate(midi_client, CFSTR("UnityMIDIReceiver Input Port"), MIDIReadProc, nullptr, &midi_port) != noErr) return false;
        
        // Enumerate the all MIDI sources.
        ItemCount sourceCount = MIDIGetNumberOfSources();
        source_ids.resize(sourceCount);
        
        for (int i = 0; i < sourceCount; i++)
        {
            MIDIEndpointRef source = MIDIGetSource(i);
            if (source == 0) return false;
            
            // Retrieve the ID of the source.
            SInt32 id;
            if (MIDIObjectGetIntegerProperty(source, kMIDIPropertyUniqueID, &id) != noErr) return false;
            source_ids.at(i) = id;
            
            // Connect the MIDI source to the input port.
            if (MIDIPortConnectSource(midi_port, source, &id) != noErr) return false;
        }
        
        reset_required = false;
        return true;
    }
    
    // Retrieve the name of a given source.
    std::string GetSourceName(uint32_t source_id)
    {
        static const char* default_name = "unknown";
        
        MIDIObjectRef object;
        MIDIObjectType type;
        if (MIDIObjectFindByUniqueID(source_id, &object, &type) != noErr) return default_name;
        
        if (type != kMIDIObjectType_Source) return default_name;
        
        CFStringRef name;
        if (MIDIObjectGetStringProperty(object, kMIDIPropertyDisplayName, &name) != noErr) return default_name;
        
        char buffer[256];
        CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
        
        return buffer;
    }
}

int main(int argc, const char * argv[])
{
    while (true)
    {
        for (int i = 0; i < 10; i++)
        {
            ResetIfRequired();
            
            message_queue_lock.lock();
            
            while (!message_queue.empty())
            {
                std::cout << message_queue.front().ToString() << std::endl;
                message_queue.pop();
            }
            
            message_queue_lock.unlock();
            
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
        }
        
        for (auto id : source_ids)
            std::cout << GetSourceName(id) << std::endl;
    }
    
    return 0;
}