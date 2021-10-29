#include "goofy.internal.h"

namespace goofy {

	template<typename S>
	bool Obj<S>::IsNull() {
		return this->__state == nullptr;
	}

#pragma region Synchronization Objects

	Semaphore::Semaphore() : Semaphore(0) {}

	Semaphore::Semaphore(int initialState) : state(initialState) {
	}

	void Semaphore::Wait() {
		std::unique_lock<std::mutex> lock(mutex);
		while (state == 0)
			waiting.wait(lock);
		state--;
	}

	void Semaphore::Signal() {
		std::unique_lock<std::mutex> lock(mutex);
		state++;
		waiting.notify_one();
	}

	void Semaphore::SignalAll() {
		std::unique_lock<std::mutex> lock(mutex);
		state++;
		waiting.notify_all();
	}

	void OneTimeSemaphore::Wait() {
		s.Wait();
		s.Signal();
	}

	void OneTimeSemaphore::Done() {
		s.SignalAll();
	}

	

#pragma endregion

}