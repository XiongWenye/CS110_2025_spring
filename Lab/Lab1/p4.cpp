#include <iostream>
#include <bitset>
#include <vector>

void displayPattern(const std::vector<int>& numbers) {
    for (int num : numbers) {
        std::bitset<5> bits(num);
        for (int i = 4; i >= 0; --i) {
            if (bits[i] == 1) {
                std::cout << "■ ";
            } else {
                std::cout << "□ ";
            }
        }
        std::cout << std::endl;
    }
}

int main() {
    std::vector<int> numbers(5);
    for (int i = 0; i < 5; ++i) {
        std::cin >> numbers[i];
    }

    std::cout << "Pattern:" << std::endl;
    displayPattern(numbers);

    return 0;
}