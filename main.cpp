#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ThreadPool.h"

using namespace std;

void task(void* arugment)
{
    sleep(1);
    cout << "TID(" << pthread_self() << "): " 
         << (size_t)arugment << "" << endl;
}

int main()
{
    // 创建一个微型线程池,线程数量为 4
    ThreadPool pool(4);

    for(int i = 0; i < 30; i++)
        pool.appendTask(task, (void*)i);

    return 0;
}