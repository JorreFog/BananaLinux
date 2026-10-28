#pragma once
// banana-ui: a few threads that share out the frame's own work (the screens' tiles, a big box's rows) with the
// thread that draws, so a frame takes the cores the handhelds have (four slow ones) instead of one. Each piece is
// worked out exactly as it would be on one thread: the frame looks the same, only sooner. BANANA_THREADS=1 draws on
// one thread (tests, comparisons)
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace banana
{
	class Pool
	{
	public:
		static Pool& get()
		{
			static Pool p;
			return p;
		}
		int threads() const { return (int)th.size() + 1; }
		// fn(0) .. fn(n - 1), on the helpers and this thread, back when all are done. Not reentrant: a call from
		// inside fn (or from two threads at once) runs serially
		void run(int n, const std::function<void(int)>& fn)
		{
			if (n <= 0) return;
			bool expected = false;
			if (th.empty() || n == 1 || !busy.compare_exchange_strong(expected, true))
			{
				for (int i = 0; i < n; i++) fn(i);
				return;
			}
			{
				std::lock_guard<std::mutex> l(m);
				job = &fn;
				count = n;
				next = 0;
				left = (int)th.size();
				gen++;
			}
			cv.notify_all();
			work();
			{
				std::unique_lock<std::mutex> l(m);
				doneCv.wait(l, [&] { return left == 0; });
				job = nullptr;
			}
			busy = false;
		}

	private:
		std::vector<std::thread> th;
		std::mutex m;
		std::condition_variable cv, doneCv;
		const std::function<void(int)>* job = nullptr;
		int count = 0, left = 0;
		std::atomic<int> next{ 0 };
		std::atomic<bool> busy{ false };
		uint64_t gen = 0;
		bool stop = false;

		Pool()
		{
			int n = (int)std::thread::hardware_concurrency();
			if (const char* e = getenv("BANANA_THREADS")) n = atoi(e);
			n = std::max(1, std::min(4, n));
			for (int i = 1; i < n; i++) th.emplace_back([this] { loop(); });
		}
		~Pool()
		{
			{ std::lock_guard<std::mutex> l(m); stop = true; }
			cv.notify_all();
			for (auto& t : th) t.join();
		}
		void work()
		{
			for (int i; (i = next.fetch_add(1)) < count;) (*job)(i);
		}
		void loop()
		{
			uint64_t seen = 0;
			for (;;)
			{
				{
					std::unique_lock<std::mutex> l(m);
					cv.wait(l, [&] { return stop || gen != seen; });
					if (stop) return;
					seen = gen;
				}
				work();
				{
					std::lock_guard<std::mutex> l(m);
					if (--left == 0) doneCv.notify_one();
				}
			}
		}
	};
}
