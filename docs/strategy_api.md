# Strategy API

## C++ Strategy Interface

The Strategy API lets you implement custom trading strategies in C++ that plug directly into the matching engine.

### Interface

```cpp
// engine/include/strategy.hpp

struct MarketState {
    Tick best_bid, best_ask, midprice;
    Quantity bid_depth, ask_depth;
    double sigma_sq;         // Estimated midprice variance
    int64_t inventory;       // Current signed inventory
    int64_t step, total_steps;
};

struct Quote {
    Tick bid_price, ask_price;
    Quantity bid_qty, ask_qty;
    bool has_bid() const;  // true if valid bid
    bool has_ask() const;  // true if valid ask
};

class Strategy {
public:
    virtual Quote generate_quote(const MarketState& state) = 0;
    virtual void on_fill(const FillMsg& fill) = 0;
    virtual void on_book_update(const BookUpdateMsg& update) {}  // optional
    virtual void on_init(int64_t total_steps) {}                 // optional
    virtual const char* name() const { return "Strategy"; }
};
```

### Example: Naive Symmetric Market Maker

```cpp
class NaiveSymmetricMM : public Strategy {
    Tick half_spread_ = 10;
    Quantity qty_ = 100;
    int64_t inventory_ = 0;

public:
    Quote generate_quote(const MarketState& s) override {
        // Don't quote if inventory is extreme.
        if (std::abs(inventory_) > 500) return {};
        return {
            s.midprice - half_spread_,
            s.midprice + half_spread_,
            qty_, qty_
        };
    }

    void on_fill(const FillMsg& fill) override {
        // Track inventory (simplified).
        inventory_ += fill.quantity;  // Adjust sign based on maker/taker side
    }

    const char* name() const override { return "NaiveSymmetricMM"; }
};
```

## Python Agent Interface

The Python `BaseAgent` class provides a higher-level interface for the simulation layer.

### Interface

```python
class BaseAgent(ABC):
    @abstractmethod
    def on_book_update(self, update: BookUpdateMsg, step: int) -> List[AgentOrder]:
        """Return a list of orders to submit based on current book state."""
        pass

    # Already provided:
    def next_order_id(self) -> int: ...
    def track_order(self, order: AgentOrder): ...
    def on_fill(self, fill_msg): ...
    
    # Stats tracking:
    self.stats.pnl
    self.stats.inventory
    self.stats.total_fills
    self.stats.total_orders
```

### Creating a Custom Agent

```python
from simulation.agents.base_agent import BaseAgent, AgentOrder

class MyStrategy(BaseAgent):
    def on_book_update(self, update, step):
        mid = (update.best_bid + update.best_ask) // 2
        spread = 10
        return [
            AgentOrder(self.next_order_id(), side=0, order_type=0,
                       price=mid - spread, quantity=100),
            AgentOrder(self.next_order_id(), side=1, order_type=0,
                       price=mid + spread, quantity=100),
        ]
```

### Adding to the Simulator

1. Create your agent class in `simulation/agents/`
2. Import it in `simulator.py`
3. Add to `_create_agents()` with a latency profile
4. Add configuration to `config/default.yaml`
