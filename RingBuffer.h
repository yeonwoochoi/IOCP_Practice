#pragma once
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>

class RingBuffer {
public:
	explicit RingBuffer(size_t size): mBufferSize(size) {
		assert((size & (size - 1)) == 0 && "size must be power of two"); // 2^n 크기만 허용 (mask 트릭 전제)
		mBuffer = new uint8_t[size];
	}

	~RingBuffer() {
		delete[] mBuffer;
	}

	size_t GetReadableSize() const { return mWritePos - mReadPos; }
	size_t GetWritableSize() const { return mBufferSize - GetReadableSize(); }

	uint8_t* GetWriteBuffer() { return mBuffer + (mWritePos & GetMask()); }
	uint8_t* GetReadBuffer() { return mBuffer + (mReadPos & GetMask()); }

	// WSARecv용: 쓰기 시작 위치부터 배열 끝까지 "연속으로" 받을 수 있는 크기
	size_t GetContinuousWritable() const {
		size_t wIdx = mWritePos & GetMask();
		return std::min(GetWritableSize(), mBufferSize - wIdx);
	}

	// 파싱용: 읽기 시작 위치부터 배열 끝까지 "연속으로" 읽을 수 있는 크기
	size_t GetContinuousReadable() const {
		size_t rIdx = mReadPos & GetMask();
		return std::min(GetReadableSize(), mBufferSize - rIdx);
	}

	void Write(size_t n) { mWritePos += n; } // 받은 만큼 쓰기 위치 전진 (커밋)
	void Read(size_t n) { mReadPos += n; }   // 소비한 만큼 읽기 위치 전진
	void Clear() { mReadPos = 0; mWritePos = 0; } // 세션 재사용 시 잔여 데이터 폐기

	// mReadPos 기준 n바이트를 dst로 복사 (전진 안 함). wrap이면 두 조각으로 이어붙임.
	void Peek(uint8_t* dst, size_t n) {
		size_t rIdx = mReadPos & GetMask();
		size_t first = std::min(n, mBufferSize - rIdx); // 끝까지 연속분
		memcpy(dst, mBuffer + rIdx, first);
		// 나머지가 끝을 넘어 앞으로 갈렸으면 앞부분 이어 붙이기
		if (first < n) {
			memcpy(dst + first, mBuffer, n - first);
		}
	}

private:
	size_t GetMask() const { return mBufferSize - 1; }

	uint8_t* mBuffer = nullptr;
	size_t mReadPos = 0;   // free-running (리셋 안 함, 접근 시 mask)
	size_t mWritePos = 0;
	size_t mBufferSize = 0;
};
