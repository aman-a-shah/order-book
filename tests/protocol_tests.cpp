#include <cstdlib>
#include <iostream>

#include "lob/binary_protocol.hpp"

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    const lob::OrderCommand input{lob::OrderType::Replace, lob::Side::Sell, 42, 10025, 17, 43, 7};
    const auto bytes = lob::encode_binary_order(input);
    lob::OrderCommand output;
    require(lob::decode_binary_order(bytes, output), "binary decode succeeds");
    require(output.type == input.type, "type round trip");
    require(output.side == input.side, "side round trip");
    require(output.order_id == input.order_id, "order id round trip");
    require(output.price == input.price, "price round trip");
    require(output.quantity == input.quantity, "quantity round trip");
    require(output.new_order_id == input.new_order_id, "new order id round trip");
    require(output.symbol_id == input.symbol_id, "symbol id round trip");
    std::cout << "Binary protocol test passed\n";
    return 0;
}
