# roadmap — the-book-was-open

20 steps from "matching engine works" to "I got the offer." organized into four phases: harden the engine, build the SLP extension, get validated, and show up ready.

timeline: aug 2026 → aug 2027.

---

## phase 1: harden (sep–oct 2026)

the engine works, but "works" and "battle-tested" are different things. this phase is about making the codebase something you can defend in an interview, not just demo.

### 1. read your own code — on paper

trace a FOK buy order through `can_fill()` → `submit()` → `match_order()` by hand. draw the book state at each step. do the same for an IOC that partially fills across two price levels. if you can't do this without looking at the code, you don't own the project yet.

**deliverable:** nothing. this is for you.
**when:** first week of sep

### 2. add self-trade prevention

real exchanges don't let the same entity trade against itself. add a `trader_id` field to `Order`, and a configurable STP mode: `CANCEL_NEWEST`, `CANCEL_OLDEST`, `CANCEL_BOTH`. emit a new `STPCancel` event when it fires.

this is a small feature but it shows you understand market microstructure beyond textbook matching.

**deliverable:** code + 4-5 tests
**when:** sep week 1-2

### 3. add L2 market depth

expose `depth(int levels)` on `Book` — returns the top N price levels with aggregated quantity on each side. update the CLI to print depth after each order if a `--depth N` flag is passed.

interviewers love asking "what does the order book look like" — this gives you a concrete answer with code behind it.

**deliverable:** `TopOfBook` → `MarketDepth` struct, CLI flag, 3-4 tests
**when:** sep week 2-3

### 4. write edge-case tests yourself

don't use me for this. sit down and think about what breaks:
- submit 10,000 orders at the same price, cancel the middle one
- FOK that needs exactly the full book
- market order on a book with 1 share at each of 100 price levels
- cancel an order that was just partially filled
- two symbols with the same order ID (should this work?)

aim for 15+ new tests that you wrote and can explain.

**deliverable:** expanded test_matching.cpp, 45+ total tests
**when:** sep week 3-4

### 5. add a code quality badge

set up SonarCloud or Codacy on the GitHub repo. get the badge green and put it in the README. takes 20 minutes but signals "I care about code quality" to anyone who visits the repo.

**deliverable:** badge in README, zero issues on dashboard
**when:** early oct

### 6. rewrite DESIGN.md in your voice

the current one is fine but it reads generic. rewrite it the way you'd explain the system to a friend who knows C++ but not finance. add a "mistakes I made" or "what I'd change" section — shows maturity.

**deliverable:** DESIGN.md that sounds like you
**when:** oct week 1

### 7. post for code review

post on r/cpp or r/algotrading: "wrote a limit order book matching engine in C++17, looking for feedback." link the repo. be specific about what you want reviewed (data structure choice, event model, anything you're unsure about). take the feedback seriously and fix what deserves fixing.

**deliverable:** reddit post, incorporated feedback
**when:** oct week 2

---

## phase 2: extend for SLP (nov 2026 – feb 2027)

this is where the standalone project becomes the foundation for your SLP. the goal is to have enough scope that an IEOR faculty member sees a real research/engineering project, not a homework assignment.

### 8. reach out to IEOR faculty

you need an external SLP guide from IEOR. the play:
- find 2-3 IEOR profs who work on market microstructure, algorithmic trading, or financial engineering
- reach out via their PhD students first (less intimidating, higher response rate)
- email with: one paragraph about you, link to the repo, one paragraph about what you want to build next
- also email the DESE UG coordinator about the external guide process

**deliverable:** emails sent, at least one conversation started
**when:** oct–nov

### 9. define the SLP scope with your guide

once you have a guide (or a strong candidate), sit down and scope the SLP extension together. potential directions:
- **strategy backtester**: replay historical order flow through your engine, measure P&L of simple strategies
- **market simulator**: generate synthetic order flow with configurable parameters (spread, volatility, participant mix), study price formation
- **low-latency optimization**: replace std::map with flat arrays + intrusive lists, benchmark the difference, write about cache effects
- **FIX protocol gateway**: thin networking layer that accepts FIX messages, translates to your Order struct

let the guide shape this — their name is on it too.

**deliverable:** 1-page SLP proposal
**when:** nov–dec

### 10. SLP registration

register during the december course registration window. you need your DESE guide + IEOR external guide sorted by then.

**deliverable:** registered SLP
**when:** dec 2026

### 11. build the FIX protocol adapter

(or whatever extension you and your guide chose — using FIX as the example.)

a thin layer that parses FIX 4.2 `NewOrderSingle` (tag 35=D) messages into your `Order` struct, and formats `ExecutionReport` (tag 35=8) from your events. no need to implement full FIX — just the order entry subset.

this makes the project legit. "I wrote a matching engine" is good. "I wrote a matching engine with FIX protocol support" makes people stop scrolling.

**deliverable:** `fix_adapter.h`, `fix_adapter.cpp`, tests, updated README
**when:** jan 2027

### 12. add order book persistence

serialize book state to a binary format. rebuild from event log replay. test that `serialize → deserialize → serialize` round-trips perfectly. this ties into the event-sourcing story and shows you understand durability.

**deliverable:** `snapshot.h`, `snapshot.cpp`, round-trip tests
**when:** jan–feb 2027

### 13. build a strategy backtester or market simulator

feed historical or synthetic order flow through the engine. measure fills, slippage, queue position. output a CSV of results that you can plot. doesn't need to be fancy — correctness and simplicity matter more than features.

**deliverable:** `sim/` directory with the simulator, sample output, a few plots
**when:** feb 2027

### 14. write the SLP report

your guide will have a format. but the content should cover: problem statement, design decisions (with alternatives you considered and rejected), implementation, testing methodology, benchmark results, and what you'd do with another semester.

**deliverable:** SLP report (likely 20-30 pages)
**when:** mar–apr 2027

---

## phase 3: validate (mar–may 2027)

### 15. DM exchange-infra engineers on LinkedIn

find 3-5 people who work on matching engines, exchange infrastructure, or low-latency systems at firms like Tower Research, Graviton, DE Shaw, WorldQuant, or any Indian HFT shop. send a short message: "I'm a B.Tech student at IIT Bombay, built a matching engine in C++17 [link], would love 15 minutes of feedback."

worst case: no reply. best case: feedback that makes the project better + a warm contact for placements.

**deliverable:** 5 messages sent, notes from any conversations
**when:** mar–apr 2027

### 16. get a code review from someone in industry

different from the reddit post. this is a 1:1 review from someone who builds these systems for a living. could come from the LinkedIn outreach, a senior at IITB who's now at a trading firm, or a connection through your SLP guide.

**deliverable:** review notes, changes made based on feedback
**when:** apr–may 2027

### 17. benchmark on production-like hardware

if you can get access to a bare-metal server (through your department, a cloud free tier, or a friend's machine), run the benchmark with `isolcpus`, `taskset`, and `perf stat`. report IPC, cache misses, branch mispredictions. this is the kind of data that makes a quant firm interviewer sit up.

**deliverable:** updated benchmark section in README with hardware-specific numbers
**when:** may 2027

---

## phase 4: show up ready (jun–aug 2027)

### 18. build your resume bullet points

don't write "built a matching engine." write something like:

> designed and implemented a C++17 limit order book matching engine with price-time priority, four order types (limit/market/IOC/FOK), and sub-microsecond median latency (p50=263ns, 2.8M orders/sec single-threaded). event-sourced architecture with FIX 4.2 protocol support.

have 2-3 variants: one for quant/HFT roles, one for general SDE roles, one for the SLP section of your transcript.

**deliverable:** resume with project section polished
**when:** jun 2027

### 19. prep for technical interviews

you should be able to answer, cold:
- "walk me through what happens when a limit buy order arrives and the book has resting asks"
- "why std::map and not a flat array? when would you switch?"
- "how does your FOK implementation guarantee atomicity without locks?"
- "what's your cache miss rate and why?"
- "how would you add multithreading? what's the contention point?"
- "explain your event model. why variant over virtual dispatch?"

practice these out loud. not in your head — out loud.

**deliverable:** you, being articulate about your own code
**when:** jun–jul 2027

### 20. target the right companies

with this project on your resume, you're positioned for:
- **quant/HFT firms**: Tower Research, Graviton, DE Shaw, WorldQuant, Quadeye, AlphaGrep
- **exchange infra**: NSE/BSE tech teams, if they recruit from campus
- **systems roles at tech companies**: any firm that values low-latency C++ (Google infrastructure, database teams)

make a shortlist of 8-10 companies. for each one, find someone on LinkedIn who works there and went to IITB. reach out in july, before placement season starts in august.

**deliverable:** target list with contacts, outreach started
**when:** jul–aug 2027

---

## timeline at a glance

```
sep 2026    code trace, self-trade prevention, L2 depth
oct 2026    edge-case tests, code quality badge, DESIGN.md rewrite, reddit post
nov 2026    IEOR faculty outreach, SLP scope discussion
dec 2026    SLP registration
jan 2027    FIX adapter, persistence
feb 2027    backtester/simulator
mar 2027    SLP report writing, LinkedIn outreach
apr 2027    industry code review
may 2027    production benchmark
jun 2027    resume polish, interview prep
jul 2027    company targeting, outreach
aug 2027    placement season
```

---

*last updated: aug 2026*
