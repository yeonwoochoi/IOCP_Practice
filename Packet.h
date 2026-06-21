#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct PacketData {
public:
	UINT32 SessionIndex = 0;
	UINT32 Generation = 0;
	UINT DataSize = 0;
	char* pPacketData = nullptr;

	PacketData() = default;
	~PacketData() { delete[] pPacketData; }

	PacketData(UINT32 sessionIndex, UINT32 gen, UINT32 dataSize, const char* pData)
		: SessionIndex(sessionIndex), Generation(gen), DataSize(dataSize)
	{
		if (dataSize > 0 && pData != nullptr) {
			pPacketData = new char[dataSize];
			CopyMemory(pPacketData, pData, dataSize);
		}
	}

	PacketData(const PacketData& other) = delete;
	PacketData& operator=(const PacketData& other) = delete;

	PacketData(PacketData&& other) noexcept
		:SessionIndex(other.SessionIndex),
		Generation(other.Generation),
		DataSize(other.DataSize),
		pPacketData(other.pPacketData)
	{
		other.pPacketData = nullptr;
		other.DataSize = 0;
	}

	PacketData& operator=(PacketData&& other) noexcept {
		if (this != &other) {
			delete[] pPacketData;
			pPacketData = other.pPacketData;
			DataSize = other.DataSize;
			Generation = other.Generation;
			SessionIndex = other.SessionIndex;

			other.pPacketData = nullptr;
			other.DataSize = 0;
		}
		return *this;
	}
};