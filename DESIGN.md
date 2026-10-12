# Design notes

## Current data structures

The current implementation uses `std::unordered_map<Price, std::unique_ptr<PriceLevel>>` for each side. Within a level, orders are connected by an **intrusive doubly linked list** (`RestingOrder::next/prev`). An `unordered_map<OrderId, RestingOrder*>` provides average O(1) lookup of active orders. `RestingOrder` nodes are owned by a vector of unique pointers and recycled using a free-node list.

Because price levels are **unordered**, `best_bid_price()` and `best_ask_price()` scan every occupied price level, O(m), and `match_order()` sorts price keys before matching, O(m log m) plus traversal of matched orders. `depth(k)` also sorts all price keys before collecting the first k levels; the complexity depends on m, not just k. Average hash lookup is O(1), with worst-case hash-collision degradation; cancelling an indexed order unlinks its intrusive node in O(1) plus average hash operations.

The earlier version of this document described a sorted `std::map` of `std::list` buckets, which is **not** the current implementation. That design would make best-price access cheaper but change memory and insertion tradeoffs.

## Performance tradeoffs

Hash-based levels simplify direct lookup and allow intrusive FIFO order queues, but best-price discovery requires a scan or an auxiliary sorted index. The per-order node recycler bounds allocations based on peak simultaneous resting orders rather than total lifetime orders, except for deliberate snapshot or other retained state.

## Order types

- **Limit**: matches while the incoming price crosses the book; any
  remainder rests at the order's own price.
- **Market**: matches at whatever price is available, walking the book
  until filled or the book runs dry. Never rests — unmatched quantity is
  cancelled.
- **IOC** (immediate-or-cancel): behaves like a limit order for the matching
  pass, but any remainder is cancelled instead of resting.
- **FOK** (fill-or-kill): atomic. `can_fill()` walks the opposite side
  read-only first to check whether the *entire* quantity is available at
  the limit price or better. If not, the order is rejected without
  touching the book at all — no partial fill, no side effects.

## Self-trade prevention (STP)

STP is configured per `Book` (and propagated per-`Exchange`, one mode for
every symbol) via `STPMode`:

- **None**: self-trades are allowed; the two orders match normally.
- **CancelNewest**: when the aggressor would trade against its own resting
  order, the aggressor is killed immediately (`STPCancel` + `CancelAck`) and
  matching stops — the resting order is untouched, and nothing before that
  point in the walk is undone.
- **CancelOldest**: the resting order is cancelled and removed from the
  book; the aggressor's walk *continues* past it against the next resting
  order at that price. This is why `can_fill()` (used for FOK) has to
  independently skip same-trader resting orders — the real match will skip
  them too.
- **CancelBoth**: both the resting order and the aggressor are killed at
  the point of collision; nothing after that point is matched.

STP only fires when the incoming order carries a non-zero `trader_id` and
the resting order's `trader_id` matches it — `trader_id == 0` is the "no
trader attached" sentinel and never triggers STP, by design (useful for
synthetic/benchmark order flow that doesn't care about self-trading).

## Order modification semantics

`Book::modify(id, new_price, new_qty, sink)` is a **cancel + re-submit**,
not an in-place price/qty edit:

1. Reject if the order doesn't exist, `new_qty <= 0`, `new_price <= 0`, or the proposed price would cross or lock the opposite best quote. The original resting order and its FIFO priority are left untouched on rejection.
2. Otherwise the old resting order is removed from its price level exactly
   like `cancel` does (no `CancelAck` is emitted for this internal removal
   — the caller only sees the final `ModifyAck`).
3. It's re-inserted at `new_price` with `new_qty`, at the **back** of that
   price level's queue — even if `new_price` is unchanged. This means a
   qty-only modify still loses queue position.
4. A single `ModifyAck{order_id, new_price, new_qty}` is emitted.

Two deliberate consequences:

- **Modify loses time priority.** This mirrors real exchange behavior
  (Nasdaq, CME, etc. all reprice-to-back on modify) and is exercised
  directly by `"modify loses time priority"` in the test suite: two orders
  resting at the same price, modify the first one's quantity, and the
  *second* order now has priority.
- **Modify is passive — it never matches.** A new price that would cross or lock the opposite best quote is rejected before unlinking the resting order. This avoids a crossed book and preserves FIFO order on rejection. To trade aggressively, cancel and submit a new order through `submit()`. The crossing check scans price levels because prices are stored in hash maps.

## L2 market depth

`Book::depth(levels)` returns a `MarketDepth{bids, asks}` snapshot: up to
`levels` `DepthLevel{price, qty, order_count}` entries per side, best price
first (bids high-to-low, asks low-to-high). `qty` is the sum of `remaining`
across every resting order at that price; `order_count` is how many orders
make up that sum. It's a plain read — O(m log m + nodes visited) per side — built by walking
the sorted copies of the hash-map price keys, so it reflects exactly the state `top()`
would report, just carried further into the book.

This is intentionally *not* incrementally maintained (no cached depth
struct kept in sync on every submit/cancel/modify) — at realistic depths
(single or low double digits of levels) recomputing on demand is cheap and
removes an entire class of bugs where the cache could drift from the real
book state.

## Event model: `std::variant` over virtual dispatch

Every book-mutating call emits zero or more `Event`s
(`Ack`, `Trade`, `Filled`, `Partial`, `CancelAck`, `Reject`, `STPCancel`,
`ModifyAck`) via `EventSink::on_event(const Event&)`, where `Event` is a
`std::variant` over those plain structs rather than a polymorphic
`EventBase` hierarchy with virtual methods. Reasons:

- **No heap allocation per event.** A `std::variant<...>` is a tagged union
  sized for its largest alternative, living on the stack (or inline inside
  `VectorSink::events`, contiguous). A virtual hierarchy needs each event
  heap-allocated behind a pointer (or a manual object-pool to avoid that),
  which matters at the >1M events/sec rates the benchmarks operate at.
- **Exhaustiveness is compiler-enforced.** `std::visit` with a set of
  overloads (see `JsonSink::write`, `PrintSink::print` in `lob_cli.cpp`)
  fails to compile if a new event type is added and a call site forgets to
  handle it — there's no virtual base with a default no-op to silently
  swallow it. Adding `ModifyAck` to the variant and to every `write`/`print`
  overload set was a compile error everywhere it was missing, not a
  silent gap.
- **Consumers only need `on_event`.** `VectorSink` (used by every test),
  `JsonSink`, and `lob_cli`'s `PrintSink` all just pattern-match on the
  variant — no separate `on_ack` / `on_trade` / ... virtual methods to
  override, no partial implementations.

The tradeoff is the usual one for closed sums: adding a new event type is a
recompile-everything change (every `visit` site must handle it), whereas a
virtual hierarchy could add a new subclass without touching existing code —
but existing code would then silently ignore that new event unless someone
remembered to add a handler. For a matching engine, "silently ignore an
event" is the worse failure mode.

## Multi-symbol exchange routing

`Exchange` owns one `Book` per symbol in an
`unordered_map<Symbol, Book>`, all constructed with the same `STPMode`.
`submit()` routes by `order.symbol` and rejects unknown symbols outright
(no implicit symbol creation on submit — `add_symbol()` is explicit).

`cancel()` and `modify()` don't know which symbol an order id belongs to
(order ids are globally unique across the exchange, not per-symbol), so
both do a linear scan across books: probe each non-empty book with a
`VectorSink`, and forward its events once a `CancelAck` / `ModifyAck` shows
up (a `Reject` from that book just means "not here, try the next one").
This is O(number of symbols) per cancel/modify, fine for the symbol counts
a single-process engine like this handles; a global `id -> symbol` index
would make it O(1) if that scan ever shows up in a profile.

## Snapshot format

Binary, defined in `snapshot.h`, magic `"LOB1"`, version byte `1`:

```
[4 bytes]  magic "LOB1"
[1 byte]   version (1)
[1 byte]   stp_mode
[4 bytes]  num_symbols (uint32)
for each symbol, sorted lexicographically for determinism:
  [2 bytes]   symbol_len (uint16)
  [N bytes]   symbol string (no null terminator)
  [4 bytes]   num_orders (uint32)
  for each order, in bid-then-ask, price-then-time order (dump_orders()):
    [8 bytes]  id        (uint64)
    [8 bytes]  trader_id (uint64)
    [1 byte]   side      (0=buy, 1=sell)
    [8 bytes]  price     (int64)
    [8 bytes]  remaining (int64)
    [8 bytes]  timestamp (uint64)
```

41 bytes per order. All integers are written with `memcpy`-style raw writes
(`write_raw` / `read_raw`), so this is little-endian on every platform this
project currently targets, and the format assumes save and load happen on
architectures with matching endianness — documented rather than solved,
since portability across mixed-endian hosts isn't a goal right now.

The format only captures *resting orders*, not the event log — it's a
point-in-time state snapshot for fast restart, not an audit trail. The
`"serialize-deserialize-serialize gives identical bytes"` and
`"event replay produces same state as snapshot"` tests in
`test_snapshot.cpp` pin down the two guarantees that make this useful: the
format round-trips exactly, and replaying the same event sequence from
empty produces a byte-identical snapshot to taking it after live matching —
i.e. the snapshot is a faithful compression of "replay this order flow from
scratch."

## Matching invariants

Checked throughout `test_matching.cpp` (see `check_no_crossed_book()` and
friends), these hold after every operation:

1. **No crossed book at rest**: `best_bid < best_ask` whenever both exist.
   Incoming orders match until they no longer cross. Passive modifications now reject crossed or locked repricing before mutating book state, and a regression test checks both sides.
2. **FIFO within a price level**: earlier arrival (`Timestamp`, effectively
   insertion order into the intrusive FIFO queue) always matches first at a given
   price. `modify()` intentionally resets this for the modified order (see
   above).
3. **No trade-through**: an aggressor never skips a resting order at a
   better price for one at a worse price — the sorted price-key walk visits price
   levels in strict best-to-worst order and only stops when the price no
   longer crosses.
4. **Quantity conservation**: for any operation,
   `qty_in == qty_matched + qty_resting + qty_cancelled`. Every `Trade`
   reduces both sides' remaining qty by exactly `fill`; every order either
   ends up `Filled`, `Partial` + resting, or `CancelAck`/`Reject` with the
   qty untouched.

## What's not here yet

- **Networking**: no wire protocol, no client/server split — everything is
  in-process, driven by direct `Book`/`Exchange` calls (or the CSV-driven
  `lob_cli`).
- **Threading**: the engine is single-threaded and makes no attempt at
  lock-free or sharded concurrent access. Multi-symbol parallelism (one
  thread per `Book`) is the natural extension since books don't share
  state, but `Exchange`'s `unordered_map<Symbol, Book>` isn't currently
  protected for concurrent symbol addition.
- **Persistence beyond snapshots**: `snapshot.h` gives point-in-time
  save/restore, not a write-ahead log or durable commit per order — a
  crash between snapshots loses everything since the last one. There's no
  append-only event journal to replay from an arbitrary point.
- **Stop / iceberg / pegged orders**, fees, risk checks, production
  market-data fan-out, regulatory controls (see also README's "Current
  scope").
- **Modify against a crossing price**: as noted above, `modify()` never
  matches even when the new price would cross — there's no test yet that
  deliberately reprices past the opposite side and asserts the book stays
  uncrossed-but-inverted (bid > ask, both resting, waiting for the next
  aggressive order to clean it up). That's the one edge of invariant (1)
  the current suite doesn't pin down.

## Performance characteristics and tradeoffs

Two benchmarks ship in `bench/`:

- **`bench_submit`**: pure limit-order insertion, 1M orders after a 10k
  warmup, price range clustered tightly around a mid (990-1010), 50/50
  buy/sell, `-O2`. This isolates raw `submit()` cost — mostly `std::map`
  level lookup/insert plus `std::list` push_back and the `unordered_map`
  index insert — with matching kept cheap by the narrow price range keeping
  most volume resting rather than crossing repeatedly.
- **`bench_mixed`**: a more realistic workload — 60% limit submits, 25%
  cancels (of a randomly chosen currently-resting order), 15% modifies
  (price nudged ±1-3 ticks, fresh random qty), 1M ops after a 10k warmup.
  Latency is reported separately per operation type, since a cancel
  (O(1) list erase) and a submit that walks several price levels have very
  different cost profiles and averaging them together would hide that. See
  the README for how to run it and what it reports.

Both report p50/p90/p99/p99.9/max/avg latency in nanoseconds and aggregate
throughput in ops/sec. Results depend on CPU, compiler, optimization level,
and system load — treat published numbers as relative, not absolute, and
always re-run locally before trusting them for a real decision. CI runs
both benchmarks for Release builds as a smoke test (they must run to
completion without crashing), not as a performance gate — there's no
regression-detection threshold wired up yet.

The dominant cost at this scale is allocator traffic: every `submit()` that
rests an order allocates a `std::list` node (and possibly a new
`std::map` node if it's a new price level); every `cancel()`/`modify()`
frees one. The "what's not here yet" flat-array-plus-intrusive-list
redesign mentioned under Data Structures is the next lever if profiling
shows the allocator as the bottleneck rather than the tree/list traversal
itself.
