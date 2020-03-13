#include <iostream>
#include <future>
#include <algorithm>
#include <mutex>
#include <vector>
#include <set>
#include <thread>
#include "FileWatch.hpp"

typedef std::chrono::duration<float, std::ratio_multiply<std::chrono::seconds::period, std::ratio<100>>> Timeout;

bool get_with_timeout(std::future<std::string>& future_to_wait_for)
{
    const auto status = future_to_wait_for.wait_for(Timeout(1));

    if(status == std::future_status::deferred)
    {
        std::cout << "Deferred\n";
        throw std::runtime_error("Deferred");
    }
    else if(status == std::future_status::timeout)
    {
        std::cout << "Timeout\n";
        throw std::runtime_error("Timeout reached");
    }
    else if(status == std::future_status::ready)
    {
        auto szTemp = future_to_wait_for.get();
        return true;
    }
    else
    {
        throw std::runtime_error("Timeout reached");
    }
}


int main()
{
    std::string test_folder_path("./test.txt");
    std::string test_file_name("test.txt");
    while (1) {

    try {
    // create the file otherwise the Filewatch will throw

    std::promise<std::string> promise;
    std::future<std::string> future = promise.get_future();

    filewatch::FileWatch<std::string> watch(test_folder_path, [&promise](const std::string& path, const filewatch::Event change_type) {
        std::cout << "Within Lambda\n";
        //std::cout << change_type << "\n";
        promise.set_value(path);
    });

    // Manually change file here

    auto path = get_with_timeout(future);
    std::cout << "RetVal : " << std::boolalpha << path << "\n";
    }
    catch (...)
    {
        std::cerr << "Error\n";
    }
    }
    return 0;
}
