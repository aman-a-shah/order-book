#pragma once

#include <array>
#include <cstddef>

#include "lob/order_book.hpp"

namespace lob {

template <std::size_t SymbolCount,
          std::size_t MaxOrdersPerSymbol,
          std::size_t MaxPriceLevelsPerSymbol,
          std::size_t IndexCapacityPerSymbol = MaxOrdersPerSymbol * 2>
class MultiBookRouter {
public:
    using Book = OrderBook<MaxOrdersPerSymbol, MaxPriceLevelsPerSymbol, IndexCapacityPerSymbol>;

    ExecutionReport process(const OrderCommand& command) {
        if (command.symbol_id >= SymbolCount) {
            ExecutionReport report;
            report.incoming_order_id = command.order_id;
            report.requested_quantity = command.quantity;
            report.rejected = true;
            return report;
        }
        return books_[command.symbol_id].process(command);
    }

    const Book& book(std::size_t symbol_id) const { return books_[symbol_id]; }
    Book& book(std::size_t symbol_id) { return books_[symbol_id]; }
    std::size_t symbol_count() const { return SymbolCount; }

private:
    std::array<Book, SymbolCount> books_{};
};

}  // namespace lob
