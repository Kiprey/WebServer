#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>

#include "ThreadPool.h"

using namespace std;

void task(void* arugment)
{
    cout << "current argument: " << (size_t)arugment << endl;
}

int main()
{
    // 创建一个微型线程池,线程数量为 4~8
    ThreadPool pool(4, 8);

    for(int i = 0; i < 16; i++)
        pool.appendTask(task, (void*)i);

    return 0;
}