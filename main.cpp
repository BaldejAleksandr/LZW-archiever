#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <filesystem>
#include <chrono>
#include <cstdint>

namespace fs = std::filesystem;

std::uintmax_t getFileSize(const std::string& path) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        std::cerr << "Error getting file size: " << ec.message() << std::endl;
        return 0;
    }
    return size;
}

std::uintmax_t getDirectorySize(const fs::path& directory) {
    std::uintmax_t size = 0;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (fs::is_regular_file(entry.status())) {
            size += fs::file_size(entry.path());
        }
    }
    return size;
}

std::vector<int32_t> compress(const std::string& text) {
    std::unordered_map<std::string, int32_t> dictionary;
    std::vector<int32_t> compressed_data;

    for (int32_t i = 0; i < 256; ++i) {
        dictionary[std::string(1, static_cast<char>(i))] = i;
    }

    std::string current;
    std::string next;
    for (char c : text) {
        next = current + c;
        if (dictionary.count(next) != 0) {
            current = next;
        } else {
            compressed_data.push_back(dictionary[current]);
            dictionary[next] = dictionary.size();
            current = c;
        }
    }

    if (!current.empty()) {
        compressed_data.push_back(dictionary[current]);
    }

    return compressed_data;
}

void save_compressed_data(const std::vector<int32_t>& compressed_data, const std::string& output_path) {
    std::ofstream output_file(output_path, std::ios::binary);
    if (!output_file.is_open()) {
        std::cerr << "Error: Unable to open output file!" << std::endl;
        return;
    }

    for (int32_t code : compressed_data) {
        output_file.write(reinterpret_cast<const char*>(&code), sizeof(int32_t));
    }

    output_file.close();
}

void compress_chunk(const std::string& chunk, const std::string& output_path) {
    std::vector<int32_t> compressed_data = compress(chunk);
    save_compressed_data(compressed_data, output_path);
}

void threading_compression(const std::vector<std::string>& chunks, const std::string& output_directory, int start_index) {
    for (size_t i = 0; i < chunks.size(); ++i) {
        std::string output_path = output_directory + "/compressed_part_" + std::to_string(start_index + i) + ".bin";
        compress_chunk(chunks[i], output_path);
    }
}

void archive(const std::string& input_file, int count_threads) {
    std::string output_directory = "output";

    if (!fs::exists(output_directory)) {
        fs::create_directory(output_directory);
    }

    std::ifstream input(input_file);
    if (!input.is_open()) {
        std::cerr << "Error: Unable to open input file!" << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    
    input.close();

    int num_chunks = 16;
    int chunk_size = text.size() / num_chunks;
    std::vector<std::string> chunks;
    for (int i = 0; i < num_chunks; ++i) {
        chunks.push_back(text.substr(i * chunk_size, chunk_size));
    }
    
    if (text.size() % num_chunks != 0) {
        chunks.back() += text.substr(num_chunks * chunk_size);
    }

    std::vector<std::thread> threads;
    int chunks_per_thread = num_chunks / count_threads;

    int chunk_index = 0;

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point stop_time;
    std::chrono::duration<double> duration;

    start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < count_threads; ++i) {
        std::vector<std::string> thread_chunks(chunks.begin() + chunk_index, chunks.begin() + chunk_index + chunks_per_thread);
        threads.emplace_back(threading_compression, thread_chunks, output_directory, chunk_index);
        chunk_index += chunks_per_thread;
    }

    for (auto& thread : threads) {
        thread.join();
    }

    stop_time = std::chrono::steady_clock::now();
    duration = stop_time - start_time;

    std::cout << "Compression complete. Compressed files are saved in directory: " << output_directory << std::endl;
    std::cout << "time: " << duration.count() << std::endl;

    auto file_size = getFileSize(input_file);
    std::cout << "input file size: " << file_size / 1024 << "KB" << std::endl;
    file_size = getDirectorySize(output_directory);
    std::cout << "output file size: " << file_size / 1024 << "KB" << std::endl;
}

std::vector<int32_t> read_compressed_data(const std::string& input_path) {
    std::ifstream input(input_path, std::ios::binary);
    if (!input.is_open()) {
        std::cerr << "Error: Unable to open input file!" << std::endl;
        return std::vector<int32_t>();
    }

    std::vector<int32_t> compressed_data;
    int32_t code;
    while (input.read(reinterpret_cast<char*>(&code), sizeof(int32_t))) {
        compressed_data.push_back(code);
    }

    input.close();
    return compressed_data;
}

std::string decompress(const std::vector<int32_t>& compressed_data) {
    std::unordered_map<int32_t, std::string> dictionary;
    std::string decompressed_text;

    for (int32_t i = 0; i < 256; ++i) {
        dictionary[i] = std::string(1, static_cast<char>(i));
    }

    int32_t prev_code = compressed_data[0];
    std::string prev_str = dictionary[prev_code];
    decompressed_text += prev_str;

    for (size_t i = 1; i < compressed_data.size(); ++i) {
        int32_t code = compressed_data[i];
        std::string str;

        if (dictionary.count(code) != 0) {
            str = dictionary[code];
        } else if (code == dictionary.size()) {
            str = prev_str + prev_str[0];
        } else {
            throw std::runtime_error("Error: Invalid compressed data!");
        }

        decompressed_text += str;
        dictionary[dictionary.size()] = prev_str + str[0];
        prev_str = str;
    }

    return decompressed_text;
}

void decompress_chunk(const std::string& input_file, std::string& output) {
    std::vector<int32_t> compressed_data = read_compressed_data(input_file);
    if (compressed_data.empty()) {
        std::cerr << "Error: Unable to read compressed data from file " << input_file << std::endl;
        return;
    }

    output = decompress(compressed_data);
}

void threading_decompression(const std::vector<std::string>& input_files, std::vector<std::string>& outputs, int start_index) {
    for (size_t i = 0; i < input_files.size(); ++i) {
        decompress_chunk(input_files[i], outputs[start_index + i]);
    }
}

void decompress_file(const std::string& input_directory, int count_threads) {
    std::vector<std::string> input_files;
    for (int i = 0; i < 16; ++i) {
        input_files.push_back(input_directory + "/compressed_part_" + std::to_string(i) + ".bin");
    }

    int num_chunks = 16;
    int chunks_per_thread = num_chunks / count_threads;
    std::vector<std::thread> threads;
    std::vector<std::string> outputs(num_chunks);

    int chunk_index = 0;

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point stop_time;
    std::chrono::duration<double> duration;

    start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < count_threads; ++i) {
        std::vector<std::string> thread_input_files(input_files.begin() + chunk_index, input_files.begin() + chunk_index + chunks_per_thread);
        threads.emplace_back(threading_decompression, thread_input_files, std::ref(outputs), chunk_index);
        chunk_index += chunks_per_thread;
    }

    for (auto& thread : threads) {
        thread.join();
    }

    stop_time = std::chrono::steady_clock::now();
    duration = stop_time - start_time;

    std::string full_text;
    for (const auto& output : outputs) {
        full_text += output;
    }

    std::string output_file_path = "output.txt";
    std::ofstream output(output_file_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        std::cerr << "Error: Unable to open output file!" << std::endl;
        return;
    }

    output << full_text;
    output.close();

    std::cout << "Decompression complete. Decompressed data is saved in: " << output_file_path << std::endl;
    std::cout << "time: " << duration.count() << std::endl;

    auto file_size = getDirectorySize(input_directory);
    std::cout << "input compressed file size: " << file_size / 1024 << "KB" << std::endl;
    file_size = getFileSize(output_file_path);
    std::cout << "output file size: " << file_size / 1024 << "KB" << std::endl;
}

int main() {
    int mode;
    std::cout << "Select mode: 1 for compression, 2 for decompression: ";
    std::cin >> mode;

    if (mode == 1) {
        int count_threads;
        std::cout << "Enter number of threads (1, 2, 4, 8, 16): ";
        std::cin >> count_threads;
        if (count_threads != 1 && count_threads != 2 && count_threads != 4 && count_threads != 8 && count_threads != 16) {
            std::cerr << "Invalid number of threads!" << std::endl;
            return 1;
        }
        std::string input_file;
        std::cout << "Enter the path to the input file: ";
        std::cin >> input_file;
        archive(input_file, count_threads);
    } else if (mode == 2) {
        int count_threads;
        std::cout << "Enter number of threads (1, 2, 4, 8, 16): ";
        std::cin >> count_threads;
        if (count_threads != 1 && count_threads != 2 && count_threads != 4 && count_threads != 8 && count_threads != 16) {
            std::cerr << "Invalid number of threads!" << std::endl;
            return 1;
        }
        std::string input_directory;
        std::cout << "Enter the path to the directory containing compressed files: ";
        std::cin >> input_directory;
        decompress_file(input_directory, count_threads);
    } else {
        std::cerr << "Invalid mode selected!" << std::endl;
        return 1;
    }

    return 0;
}