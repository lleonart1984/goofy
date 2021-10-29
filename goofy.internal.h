#ifndef GOOFY_INTERNAL_H
#define GOOFY_INTERNAL_H

#include <mutex>
#include <condition_variable>
#include <thread>

#include "goofy.h"

using namespace std;

namespace goofy {

	class Semaphore {
		std::mutex mutex;
		std::condition_variable waiting;
		int state;
	public:
		Semaphore();

		Semaphore(int initialState);

		void Wait();

		void Signal();

		void SignalAll();
	};

	class OneTimeSemaphore {
		Semaphore s;
	public:
		void Wait();

		void Done();
	};

	template<typename T>
	class ProducerConsumerQueue {
		std::vector<T> elements;
		int start;
		int count;
		Semaphore productsSemaphore;
		Semaphore spacesSemaphore;
		std::mutex mutex;
	public:
		ProducerConsumerQueue(int capacity) :
			productsSemaphore(Semaphore(0)),
			spacesSemaphore(Semaphore(capacity)) {
			elements.resize(capacity);
			start = 0;
			count = 0;
		}

		inline int getCount() { return count; }

		T Consume()
		{
			productsSemaphore.Wait();
			// ensured there is an element to be consumed
			mutex.lock();
			T element = elements[start];
			elements[start] = {};
			start = (start + 1) % elements.size();
			count--;
			spacesSemaphore.Signal();
			mutex.unlock();
			return element;
		}

		void Produce(T element) {
			spacesSemaphore.Wait();
			mutex.lock();
			int enqueuePos = (start + count) % elements.size();
			elements[enqueuePos] = element;
			count++;
			productsSemaphore.Signal();
			mutex.unlock();
		}
	};

}

#endif
