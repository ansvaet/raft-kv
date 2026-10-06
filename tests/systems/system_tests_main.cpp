#include <iostream>

// Прототипы функций
void functional_test();
void stress_test();
void reliability_test();
void config_test();
void test_put_command_tdd();    

int main() {
    functional_test();
    stress_test();
    reliability_test();
    config_test();
    test_put_command_tdd();
    std::cout << "\n✅ Все системные тесты пройдены.\n";
    return 0;
}