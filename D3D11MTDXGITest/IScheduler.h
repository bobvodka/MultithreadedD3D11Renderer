#pragma once

#include <functional>

// Simple scheduler interface
// void (float deltaTime)
typedef std::function<void (float)> UpdateFuncType;


struct IScheduler
{
	virtual void ProcessTaskQueue(float) = 0;
	virtual void QueueTask(const UpdateFuncType &updateFunc) = 0;
};