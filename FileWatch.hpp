/*
** Brief : File watcher for Pix platform
*/
#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#if __unix__
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#endif // __unix__

#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <vector>
#include <array>
#include <map>
#include <system_error>
#include <string>
#include <algorithm>
#include <type_traits>
#include <future>
#include <regex>

namespace filewatch {
enum class Event {
    added,
    removed,
    modified,
    renamed_old,
    renamed_new
};

std::ostream& operator << (std::ostream& os, const Event& obj)
{
   os << static_cast<std::underlying_type<Event>::type>(obj);
   return os;
}

/**
    * \class FileWatch
    *
    * \brief Watches a folder or file, and will notify of changes via function callback.
    *
    * \author Thomas Monkman
    *
    */
template<class T>
class FileWatch
{
    typedef std::basic_string<typename T::value_type, std::char_traits<typename T::value_type>> UnderpinningString;
    typedef std::basic_regex<typename T::value_type, std::regex_traits<typename T::value_type>> UnderpinningRegex;

public:

    FileWatch(T path, UnderpinningRegex pattern, std::function<void(const T& file, const Event event_type)> callback) :
        _path(path),
        _pattern(pattern),
        _callback(callback),
        _directory(get_directory(path))
    {
        init();
    }

    FileWatch(T path, std::function<void(const T& file, const Event event_type)> callback) :
        FileWatch<T>(path, UnderpinningRegex(".*"), callback) {}

    ~FileWatch() {
        destroy();
    }

    FileWatch(const FileWatch<T>& other) : FileWatch<T>(other._path, other._callback) {}

    FileWatch<T>& operator=(const FileWatch<T>& other)
    {
        if (this == &other) { return *this; }

        destroy();
        _path = other._path;
        _callback = other._callback;
        _directory = get_directory(other._path);
        init();
        return *this;
    }

    // Const memeber varibles don't let me implent moves nicely, if moves are really wanted std::unique_ptr should be used and move that.
    FileWatch<T>(FileWatch<T>&&) = delete;
    FileWatch<T>& operator=(FileWatch<T>&&) & = delete;

private:
    struct PathParts
    {
        PathParts(T directory, T filename) : directory(directory), filename(filename) {}
        T directory;
        T filename;
    };
    const T _path;

    UnderpinningRegex _pattern;

    static constexpr std::size_t _buffer_size = { 1024 * 256 };

    // only used if watch a single file
    bool _watching_single_file = { false };
    T _filename;

    std::atomic<bool> _destory = { false };
    std::function<void(const T& file, const Event event_type)> _callback;

    std::thread _watch_thread;

    std::condition_variable _cv;
    std::mutex _callback_mutex;
    std::vector<std::pair<T, Event>> _callback_information;
    std::thread _callback_thread;

    std::promise<void> _running;

#if __unix__
    struct FolderInfo {
        int folder;
        int watch;
    };

    FolderInfo  _directory;

    const std::uint32_t _listen_filters = IN_MODIFY ;//| IN_CREATE | IN_DELETE;

    const static std::size_t event_size = (sizeof(struct inotify_event));
#endif // __unix__

    void init()
    {
        _callback_thread = std::move(std::thread([this]() {
            try {
                callback_thread();
            } catch (...) {
                try {
                    _running.set_exception(std::current_exception());
                }
                catch (...) {} // set_exception() may throw too
            }
        }));
        _watch_thread = std::move(std::thread([this]() {
            try {
                monitor_directory();
            } catch (...) {
                try {
                    _running.set_exception(std::current_exception());
                }
                catch (...) {} // set_exception() may throw too
            }
        }));

        std::future<void> future = _running.get_future();
        future.get(); //block until the monitor_directory is up and running
    }

    void destroy()
    {
        _destory = true;
        _running = std::promise<void>();

        inotify_rm_watch(_directory.folder, _directory.watch);

        _cv.notify_all();
        _watch_thread.join();
        _callback_thread.join();

        close(_directory.folder);

    }

    const PathParts split_directory_and_file(const T& path) const
    {
        const auto predict = [](typename T::value_type character) {

            return character == '/';

        };

        const UnderpinningString this_directory = "./";


        const auto pivot = std::find_if(path.rbegin(), path.rend(), predict).base();
        //if the path is something like "test.txt" there will be no directoy part, however we still need one, so insert './'
        const T directory = [&]() {
            const auto extracted_directory = UnderpinningString(path.begin(), pivot);
            return (extracted_directory.size() > 0) ? extracted_directory : this_directory;
        }();
        const T filename = UnderpinningString(pivot, path.end());
        return PathParts(directory, filename);
    }

    bool pass_filter(const UnderpinningString& file_path)
    {
        if (_watching_single_file) {
            const UnderpinningString extracted_filename = { split_directory_and_file(file_path).filename };
            //if we are watching a single file, only that file should trigger action
            return extracted_filename == _filename;
        }
        return std::regex_match(file_path, _pattern);
    }

#if __unix__

    bool is_file(const T& path) const
    {
        struct stat statbuf = {};
        if (stat(path.c_str(), &statbuf) != 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        return S_ISREG(statbuf.st_mode);
    }

    FolderInfo get_directory(const T& path)
    {
        const auto folder = inotify_init();
        if (folder < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        const auto listen_filters = _listen_filters;

        _watching_single_file = is_file(path);

        const T watch_path = [this, &path]() {
            if (_watching_single_file)
            {
                const auto parsed_path = split_directory_and_file(path);
                _filename = parsed_path.filename;
                return parsed_path.directory;
            }
            else
            {
                return path;
            }
        }();

        const auto watch = inotify_add_watch(folder, watch_path.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE);
        if (watch < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        return { folder, watch };
    }

    void monitor_directory()
    {
        std::vector<char> buffer(_buffer_size);

        _running.set_value();
        while (_destory == false)
        {
            const auto length = read(_directory.folder, static_cast<void*>(buffer.data()), buffer.size());
            if (length > 0)
            {
                int i = 0;
                std::vector<std::pair<T, Event>> parsed_information;
                while (i < length)
                {
                    struct inotify_event *event = reinterpret_cast<struct inotify_event *>(&buffer[i]); // NOLINT
                    if (event->len)
                    {
                        const UnderpinningString changed_file{ event->name };
                        if (pass_filter(changed_file))
                        {
                            if (event->mask & IN_CREATE)
                            {
                                parsed_information.emplace_back(T{ changed_file }, Event::added);
                            }
                            else if (event->mask & IN_DELETE)
                            {
                                parsed_information.emplace_back(T{ changed_file }, Event::removed);
                            }
                            else if (event->mask & IN_MODIFY)
                            {
                                parsed_information.emplace_back(T{ changed_file }, Event::modified);
                            }
                        }
                    }
                    i += event_size + event->len;
                }
                //dispatch callbacks
                {
                    std::lock_guard<std::mutex> lock(_callback_mutex);
                    _callback_information.insert(_callback_information.end(), parsed_information.begin(), parsed_information.end());
                }
                _cv.notify_all();
            }
        }
    }
#endif // __unix__

    void callback_thread()
    {
        while (_destory == false) {
            std::unique_lock<std::mutex> lock(_callback_mutex);
            if (_callback_information.empty() && _destory == false) {
                _cv.wait(lock, [this] { return _callback_information.size() > 0 || _destory; });
            }
            decltype(_callback_information) callback_information = {};
            std::swap(callback_information, _callback_information);
            lock.unlock();

            for (const auto& file : callback_information) {
                if (_callback) {
                    try
                    {
                        _callback(file.first, file.second);
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }
        }
    }
};
}
#endif
