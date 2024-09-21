#pragma once

/*
	Author of comment: 0x90
	Title: HMW/H2-Mod Scheduling System

	Description:
	Choosing the optimal pipeline/thread for the different tasks is cruicial for a good / smooth user experience. 
	Asynchronous in this context means pipelines/threads completly unrelated to the game threads. We should call them HMW client threads.
	The base delay of any pipeline is 10ms if not manually adjusted.

	Guidelines for use:
	1. Always assign tasks to the appropriate pipeline to maintain smooth game performance and avoid bottlenecks.
	2. Tasks related to the game should use the appropriate game related thread.
		- renderer -> The game's rendering pipeline
		- server -> The game's server thread
		- main -> The game's main thread
		- lui -> For the lui context
	3. Anything unrelated to the actual game which is loaded within memory we should definetly use our HMW client threads/pipelines.
		- async -> Asynchronuous pipeline, disconnected from the game, our main thread for the HMW client
		- network -> HMW client network related pipeline, mainly for IO bound tasks related to web requests (base delay of 50ms instead of 10ms)
	4. For HMW internal dev's anticheat related pipelines do not use for anything else, timing matters alot there
*/

namespace scheduler
{
	enum pipeline
	{
		// Asynchronuous pipeline, disconnected from the game
		async = 0,

		//network related pipeline, mainly for IO bound tasks related to web requests
		//base delay of 50ms instead of 10ms, to reduce thread idle resource usage
		network,

		// The game's rendering pipeline
		renderer,

		// The game's server thread
		server,

		// The game's main thread
		main,

		// LUI context
		lui,

		count,
	};

	static const bool cond_continue = false;
	static const bool cond_end = true;

	void schedule(const std::function<bool()>& callback, pipeline type = pipeline::async,
		std::chrono::milliseconds delay = 0ms);
	void loop(const std::function<void()>& callback, pipeline type = pipeline::async,
		std::chrono::milliseconds delay = 0ms);
	void once(const std::function<void()>& callback, pipeline type = pipeline::async,
		std::chrono::milliseconds delay = 0ms);
	void on_game_initialized(const std::function<void()>& callback, pipeline type = pipeline::async,
		std::chrono::milliseconds delay = 0ms);
}