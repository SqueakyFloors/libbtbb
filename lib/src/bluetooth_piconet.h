/* -*- c -*- */
/*
 * Copyright 2007 - 2013 Dominic Spill, Michael Ossmann, Will Code
 *
 * This file is part of libbtbb
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libbtbb; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#ifndef INCLUDED_BLUETOOTH_PICONET_H
#define INCLUDED_BLUETOOTH_PICONET_H
#include "btbb.h"

/* maximum number of hops to remember */
#define MAX_PATTERN_LENGTH 1000

/* number of channels in use */
#define BT_NUM_CHANNELS 79

/* Nmin, the fewest used channels AFH is defined for
 * (Bluetooth Core v5.1 | Vol 2, Part B, Section 2.3.1). */
#define AFH_MIN_USED_CHANNELS 20

/*
 * How a CLK1-27 candidate is scored, and what it takes to accept one.
 *
 * Every observed hop is compared with the hop a candidate predicts, and the
 * comparison has three outcomes rather than two.  It agrees, where the
 * predicted channel is the one the packet arrived on.  It misses, where the
 * prediction is definitely something else.  Or it says nothing, which is the
 * case adapted hopping adds: a piconet keeps the channel its selection kernel
 * picked where its own map marks that channel used and replaces it otherwise,
 * and an observer learns a channel is used only by hearing a packet on it, so
 * a prediction landing on a channel never heard cannot be checked either way.
 *
 * The piconet's own clock agrees with every hop that can be checked against
 * it, as long as the map the prediction was made under holds no channel the
 * piconet has dropped.  Three things break that in a real capture.  A
 * controller reclassifies channels while the capture runs, and an observer
 * cannot tell a channel it heard an hour ago from one still in use, so a hop
 * the piconet now re-maps is checked against a channel it has stopped using
 * and reads as a miss.  A receiver reports the channel it was listening on,
 * which is not always the one the packet was sent on.  And the two clocks
 * drift, which moves a prediction by a slot pair every minute or so.
 * Measured on a 180 s capture of an adapted link, the best-fitting clock
 * missed 30 of the 166 hops that could be checked against it.  So one miss
 * cannot rule a candidate out: eliminating on a single miss drops the
 * piconet's own clock and leaves the search to accept whichever wrong
 * candidate outlived it.
 *
 * What separates the two instead is how much each candidate explains.  A
 * candidate that is not the piconet's clock predicts an unrelated channel, so
 * it agrees with about one hop in 79 and misses nearly all the rest; the
 * piconet's clock agrees with every hop the observer's map can check.  So the
 * score is agreements less CLK27_MISS_WEIGHT for each miss, a candidate stands
 * while that score has not fallen CLK27_MISS_GRACE below zero, and a clock is
 * accepted only when its score has reached CLK27_MIN_SCORE and is
 * CLK27_SCORE_MARGIN clear of every other candidate.
 *
 * The margin is what keeps a wrong clock from being confirmed where the hops
 * do not single one out.  Measured on that same capture, which carries voice
 * on a 7.5 ms interval so its hops are sampled on a 12-slot grid, three
 * unrelated clocks each explain 49 of 60 consecutive hops and predict
 * different channels elsewhere.  Scored, they come out level, no margin
 * opens, and the search ends with no clock rather than one of the three.
 */
#define CLK27_MISS_WEIGHT 2
#define CLK27_MISS_GRACE 6
#define CLK27_MIN_SCORE 8
#define CLK27_SCORE_MARGIN 4

/*
 * What a candidate has to score when it is the only one left.
 *
 * The margin asks whether anything else explains the hops nearly as well, and
 * with no other candidate standing there is nothing to ask.  That is not the
 * same as an answer: every clock the observer could check has been ruled out,
 * but the piconet's own may never have been in the list - the hop the
 * candidates were chosen from can be one the piconet re-mapped - and then the
 * last one standing is a clock that merely outlived the rest, which is the
 * acceptance this search was written to replace.  So a sole candidate has to
 * explain considerably more before it is believed on its own.
 */
#define CLK27_SOLE_SCORE 24

/*
 * How many hops in a row one clock has to stay clearly ahead before it is
 * accepted.
 *
 * A lead measured on one hop can be luck.  Two clocks whose predictions agree
 * over the stretch observed so far trade places as the hops come in, and
 * either of them can be a few agreements up at the moment the margin is
 * looked at.  Measured over 18 address and clock combinations, a lead that was
 * only a hop old accepted a clock that was not the device's in one of them, at
 * a score of 15 against 11.  A lead that has held over this many hops is a
 * different thing: each hop after the first is another chance for the other
 * clock to catch up, and the clock that really explains the capture pulls away
 * instead.
 */
#define CLK27_LEAD_HOPS 8

/*
 * How far the observer's clock and the piconet's may be taken to have drifted
 * apart since the candidates were chosen.
 *
 * The two clocks run at their own rates, so a prediction made from a
 * candidate slowly moves away from the piconet's real hop.  Measured on the
 * bench, the two are within a part per million of each other, which is about
 * a slot a minute, so two steps cover a capture of several minutes.  Each
 * candidate carries its own drift, counted in steps away from where it
 * started; a step is one 625 us slot, or a pair of them under adapted
 * hopping, where the same channel mechanism gives both slots of a pair one
 * channel so a single slot changes no prediction.
 *
 * Three things hold the drift to what a clock can really do, and each closes
 * a way the search would otherwise go wrong.
 *
 * Each step takes CLK27_DRIFT_SLOTS of observation to earn, so a candidate
 * cannot have drifted further than the capture has had time to drift.  16384
 * slots is 10 seconds, which allows about 6 slots a minute where the bench
 * measures 1, and leaves a short capture no drift at all.  Without it a
 * candidate may slide at every hop, which reaches far enough to explain almost
 * any capture: on the synthetic sweep that let a clock 0x2140400 away from the
 * device's explain all 101 of its checkable hops, exactly as many as the
 * device's own.
 *
 * A step is taken only when the candidate's last two readable hops have both
 * agreed at it.  One hop agrees a step out by chance about once in 79, and a
 * step is never given back, so a candidate that moved on a chance agreement
 * misses every checkable hop afterwards and dies - which for the piconet's own
 * clock is the search throwing away the right answer.  The first of the two
 * hops is still a miss, so the step costs the candidate something either way
 * and no hop has a free way out.
 *
 * And the drift only ever moves away from where it started, because one of the
 * two clocks runs faster than the other and keeps doing so.  A candidate
 * cannot wander back and forth looking for a fit.
 *
 * Together those make this a quiet-link mechanism, and deliberately: on a busy
 * link an attempt is over in a couple of seconds and neither clock has moved,
 * so no step is ever earned; on a link of a packet or two a second an attempt
 * spans minutes, which is where the clocks do move and where a step pays for
 * itself.
 */
#define CLK27_MAX_DRIFT_STEPS 2
#define CLK27_DRIFT_SLOTS 16384

/*
 * How many observed hops one attempt at CLK1-27 may take before it is given
 * up and discovery starts over.
 *
 * An attempt that has not produced a clear winner by now is not going to: the
 * hops it is being given do not single out one clock, which is what a link
 * whose packets fall on a regular grid does.  Starting over picks a different
 * first packet, and the clocks already followed and given up are remembered,
 * so the next attempt is not the same search again.
 *
 * Generous on purpose, because the alternative to waiting is accepting
 * something: 256 hops is a couple of seconds on a busy link and a minute or
 * two on a quiet one.
 */
#define CLK27_ATTEMPT_HOPS 256

/*
 * How many packet headers in a row have to fail their HEC while following a
 * piconet before the clock the library recovered is given up.
 *
 * Following starts once a clock is accepted, and from then on nothing
 * winnows: a clock that was wrong is used for every packet that follows.
 * Every candidate shares the CLK1-6 that discovery found, so what a wrong
 * candidate gets wrong is which channel to listen on, not the dewhitening.
 * The clock bits a header does depend on are CLK1-6 and the UAP, and those
 * can be wrong, accepted by elimination rather than by a CRC.  Then every
 * header fails and, until now, there was no way back.
 *
 * Only the HEC counts, not any failure to decode: it is checked against the
 * piconet's UAP over a header dewhitened with CLK1-6, so it is evidence about
 * exactly the two things being given up here.  A payload that will not decode
 * says nothing about either, and counting those would throw away a good clock
 * on a link with bit errors in it.
 *
 * The run still has to be long enough that bit errors in the headers
 * themselves do not cross it on a clock that is right.  What protects it
 * there is the access code search the packet came through, which allows a
 * couple of bit errors in 64: a packet noisy enough to break its own
 * majority-voted header hardly ever gets that far.  Measured over a run of
 * noisy packets (tests/test_clock_sweep.c), of the headers that reach the HEC
 * check under the right clock and UAP, 0.13% fail it at a bit error rate of
 * 0.5% and 29% fail at 10%, where the access code already rejects 96 packets
 * in 100.  Twenty in a row is 2e-58 and 2e-11 of a chance in those two
 * cases.  Against a wrong CLK1-6 or UAP it costs almost nothing: the HEC is 8
 * bits, so about one header in 256 passes by chance and twenty failures in a
 * row arrive in about twenty packets.
 *
 * The run is counted in packets, not in seconds, so on a link quiet enough
 * that twenty headers take hours it is twenty headers all the same.  The
 * rates above are what makes that safe rather than any time bound.
 */
#define FOLLOW_HEADER_FAILURES 20

/*
 * How long a run of failing headers has to have been going, in 625 us slots,
 * for fewer of them to be enough, and how many that is.  3200 slots is 2
 * seconds.
 *
 * At the traffic a followed link carries, FOLLOW_HEADER_FAILURES headers
 * arrive in a few tens of milliseconds, so that count alone gives a wrong
 * clock up well inside a second.  A slow link is what this is for: there
 * twenty headers can take minutes, and all that time the caller's receiver is
 * hopping on an offset that has never decoded anything.  Once the run has
 * been failing this long, half the count is taken as enough: at four packets
 * a second that is the 2.3 s tests/test_clock_sweep.c measures, where twenty
 * headers at that rate would have taken 4.8.
 *
 * This halves the packets, not the time: the library is called per packet and
 * keeps no timer, so a link slow enough still waits for its tenth failing
 * header, and a wrong clock that hears nothing at all leaves nothing to count
 * at all.  Seeing that needs the caller, which knows how long it has been
 * following in wall-clock time.
 *
 * Fewer failures is weaker evidence, which is why this path needs the time as
 * well: a run that reaches it has been failing for seconds with not one
 * header checking out.  Measured over noisy packets, the longest run of
 * failed headers under the right clock and UAP is three in 20000 packets at a
 * bit error rate of 10%, where the access code search already rejects 96
 * packets in 100; ten in a row is 4e-6 of a chance there and 1e-17 at a bit
 * error rate of 2%.  The same measurement runs those packets through the
 * library itself, at the slowest rate this bound is measured at, and no clock
 * that was right is ever given up.
 */
#define FOLLOW_FAILURE_SLOTS 3200
#define FOLLOW_FAILURES_IN_TIME 10

/*
 * How many clock offsets a piconet remembers having given up, and how many
 * clocks may be given up before the library says it cannot find one.
 *
 * Discovery starting over used to have no memory of the clock that had just
 * failed, so it could choose the same one again, accept it again, fail to
 * decode again, and go round that loop for as long as the capture ran.  A
 * rejected clock is kept out of the candidate list instead, so each attempt
 * rules out one more of them.
 *
 * The list is deliberately short.  A clock is rejected on the evidence of one
 * follow attempt, which is enough to say it never decoded a header and not
 * enough to be kept forever, so the oldest entry is dropped once the list is
 * full and what is remembered is always the clocks that failed most recently.
 * Each entry carries the UAP it was rejected under, because a rejection is
 * only about the hops that UAP predicts.
 */
#define MAX_REJECTED_CLOCKS 8
#define LOCK_LOSS_TRIES 3

/*
 * How well one CLK1-27 candidate explains the hops observed so far, and where
 * its clock has drifted to.  One of these per entry of clock_candidates, the
 * two arrays being the same length and only ever indexed together.
 *
 * Counted in hops, so a byte each is plenty: a candidate is accepted or gone
 * long before either reaches 255, and both stop counting there rather than
 * wrap.
 */
struct clk27_fit {
	uint8_t agreements;	/* hops the candidate predicted exactly */
	uint8_t misses;		/* hops it was definitely wrong about */
	int8_t drift;		/* steps, within CLK27_MAX_DRIFT_STEPS */
	int8_t pending;		/* a step one hop has agreed at, not yet taken */
};

struct btbb_piconet {

	uint32_t refcount;

	uint32_t flags;

	/* AFH channel map - either read or derived from observed packets */
	uint8_t afh_map[10];

	/* Number of entries in afh_bank, i.e. channels 0-78 the map marks
	 * used.  Maintained by precalc(), which is the only writer. */
	uint8_t used_channels;

	/* lower address part (of the Central's BD_ADDR) */
	uint32_t LAP;

	/* upper address part (of the Central's BD_ADDR) */
	uint8_t UAP;

	/* non-significant address part (of the Central's BD_ADDR) */
	uint16_t NAP;

	/* CLK1-27 candidates */
	uint32_t *clock_candidates;

	/*
	 * How each of them is doing, allocated with the candidate list and
	 * the same length.
	 *
	 * The list is at its longest when the observer has heard little of
	 * the piconet's map, because then most predictions land on a channel
	 * it cannot check and almost nothing is ruled out.  Measured: 1592763
	 * candidates at the 20 used channels AFH is defined for, 1061611 at
	 * 40, and 26623 where all 79 are used and every hop can be checked.
	 * With the clock values beside them that is 12.7 MB, 8.5 MB and
	 * 213 kB.  The walk that finds them doubles the lists as it goes and
	 * holds the old pair while it copies, so the peak is about half again
	 * as much.  Carrying those candidates is the point: that is where the
	 * piconet's own clock is.
	 *
	 * It costs the caller time as well as memory, in the packet callback:
	 * about 29 ms to score one hop against a fresh 40-channel list, and
	 * about 180 ms over a whole attempt as the list narrows, against
	 * about 2 ms before.  The library walks 2^21 clock values once per
	 * attempt either way, and the search now needs far fewer attempts, so
	 * a capture costs less overall.
	 */
	struct clk27_fit *candidate_fits;

	/* these values for single_hop() can be precalculated */
	int b, e;

	/* these values for single_hop() can be precalculated in part (e.g. a1
	 * is the precalculated part of a) */
	int a1, c1, d1;

	/* Basic mapping table (Vol 2, Part B, Section 2.6.2): even channels in
	 * ascending order followed by odd channels.  AFH consults it first, so
	 * it always describes the full basic sequence. */
	int bank[BT_NUM_CHANNELS];

	/* AFH mapping table (2.6.3.1): the basic table with every unused
	 * channel removed, holding used_channels entries. */
	int afh_bank[BT_NUM_CHANNELS];

	/* number of candidates for CLK1-27 */
	int num_candidates;

	/* Whether hop selection was adapted to the piconet's map when
	 * clock_candidates was chosen.  A piconet that has turned out to be
	 * adapted since would be scored against the basic sequence, which it
	 * does not follow.  Which map it is does not come into it: a channel
	 * the map gains cannot bring back a candidate, because a candidate is
	 * only ever ruled out on a channel the map already held. */
	uint8_t candidates_afh;

	/* How many channels the observer had not heard when clock_candidates
	 * was chosen.  Every one of them makes a hop the search cannot check,
	 * so the list is roughly that long: while the search has nothing to
	 * lose, the candidates are chosen again once the map has halved the
	 * number, which halves the list with it. */
	uint8_t candidates_unheard;

	/* Where the last packet of this piconet was heard: the observer's
	 * clock as CLK1-27 and the channel, with got_last_hop set once there
	 * has been one.  The adapted-hopping test reads a channel repeated
	 * one slot later from these, and they are kept across discovery
	 * starting over so that a restart does not take the evidence with
	 * it. */
	uint32_t last_hop_time;
	uint8_t last_hop_channel;
	uint8_t got_last_hop;

	/* The last CLK1-6 offset discovery settled on, and whether it ever
	 * has.  Kept across discovery starting over for the slot parity the
	 * adapted-hopping test needs; see central_slot(). */
	int settled_clk6_offset;
	uint8_t got_settled_clk6;

	/* number of packets observed during one attempt at UAP/clock discovery */
	int packets_observed;

	/* total number of packets observed */
	int total_packets_observed;

	/* number of observed packets that have been used to winnow the candidates */
	int winnowed;

	/* Where the search stands after the last hop: the clock the best
	 * candidate now stands for, its score, and the score of the best of
	 * the others, which is what CLK27_SCORE_MARGIN is measured between.
	 * Kept so that a caller or a test can see how close the search came
	 * and to which clock.  They describe the state num_candidates counts,
	 * so they mean nothing while that is zero, where best_clock reads
	 * SEQUENCE_LENGTH, which no clock can be. */
	uint32_t best_clock;
	int best_score;
	int runner_up_score;

	/* Hops in a row that the same clock has been clearly ahead for,
	 * counted against CLK27_LEAD_HOPS. */
	int lead_hops;

	/* How far through the remembered hops this attempt has ever scored,
	 * counted against CLK27_ATTEMPT_HOPS.  Its own counter because
	 * neither of the other two says this: winnowed goes back to the first
	 * hop whenever the candidates are chosen again, so it would hand the
	 * attempt its budget afresh, and packets_observed counts the packets
	 * UAP and CLK1-6 discovery took before hop reversal began, which the
	 * attempt has not scored yet when it starts. */
	int attempt_hops;

	/* True once hop reversal has accepted a clock for this piconet, which
	 * is the clock that can be given up again.  A caller that set the
	 * clock itself, from an adapter say, owns it: the library has no
	 * better one to fall back on, so it does not take that one away. */
	uint8_t clock_recovered;

	/* Packet headers in a row that failed their HEC while following,
	 * counted against FOLLOW_HEADER_FAILURES above. */
	int follow_header_failures;

	/* The clock the first failure of that run arrived at, as the packet
	 * carried it, so that how long the run has been going is known in
	 * slots. */
	uint32_t first_failure_time;

	/* True once a header checked out under the clock being followed: that
	 * clock has decoded something, whatever happens later, so giving it
	 * up does not mean it was never the piconet's. */
	uint8_t lock_worked;

	/* Clocks given up for this LAP, as offsets between the piconet's
	 * clock and the observer's in 625 us slots, each with the UAP it was
	 * followed under.  An offset survives discovery starting over, where
	 * a candidate CLK1-27 value could not: candidates are clocks at the
	 * first packet of an attempt, and the next attempt has a different
	 * first packet.  Cleared when the LAP changes, those being another
	 * piconet's clocks.
	 *
	 * The UAP matters because a rejection means the clock or the UAP was
	 * wrong without saying which: both go into the address hop selection
	 * runs on.  So a rejected clock is only kept out of a search that is
	 * using the same UAP; discovery that has gone on to find a different
	 * one is predicting different hops, and the old rejection says
	 * nothing about them. */
	uint32_t rejected_offsets[MAX_REJECTED_CLOCKS];
	uint8_t rejected_uaps[MAX_REJECTED_CLOCKS];
	int num_rejected;

	/* How many clocks have been given up for this LAP, and whether the
	 * library has already said it cannot find one that works. */
	int lock_losses;
	uint8_t lock_loss_reported;

	/* CLK1-6 candidates */
	int clock6_candidates[64];

	/* remember patterns of observed hops */
	int pattern_indices[MAX_PATTERN_LENGTH];
	uint8_t pattern_channels[MAX_PATTERN_LENGTH];

	/* Offset between CLKN (local) and CLK of piconet, as
	 * btbb_piconet_get_clk_offset() reports it: 312.5 us CLKN ticks once
	 * BTBB_CLK27_VALID is set, and until then the 625 us slots of
	 * clk6_offset below, which is all the search has recovered.  Nothing
	 * in the library reads it in the first form; everything internal
	 * works from clk6_offset and the candidate clocks. */
	int clk_offset;

	/* Offset between the local and the piconet clock in 625 us CLK1-6
	 * slots, 6 bits wide.  Found first, while BTBB_CLK6_VALID is set and
	 * CLK1-27 is still being searched for, so it is a separate field:
	 * masking clk_offset's CLKN ticks to 6 bits would not give slots. */
	int clk6_offset;

	/* The observer's own clock when the first packet arrived, as CLK1-27:
	 * a count of 625 us slots, the same unit as btbb_packet's clkn, which
	 * is where it comes from.  Candidate clocks, the hop differences in
	 * pattern_indices and clk6_offset are all in those slots too; the
	 * only clock here in 312.5 us CLKN ticks is clk_offset above, which
	 * is what a receiver is told to hop on. */
	uint32_t first_pkt_time;

	/* queue of packets to be decoded */
	pkt_queue *queue;
};

/* the number of CLK1-27 values, which is also the length of a full hopping
 * sequence: hops happen at every other clock value */
#define SEQUENCE_LENGTH 134217728

/* do all the precalculation that can be done before knowing the address */
void precalc(btbb_piconet *pnet);

/* do precalculation that requires the address */
void address_precalc(int address, btbb_piconet *pnet);

/* Fill the 512 KB table fast_perm() reads, once. btbb_init() calls it, so
 * that the table is whole before a caller starts any thread. */
void perm_table_init(void);

/* drop-in replacement for perm5() using lookup table */
int fast_perm(int z, int p_high, int p_low);

/* 5 bit permutation */
/* assumes z is constrained to 5 bits, p_high to 5 bits, p_low to 9 bits */
int perm5(int z, int p_high, int p_low);

/* The channel a particular hop lands on, computed when it is asked for: the
 * piconet's current AFH map is part of the answer, and that map is still being
 * learned while a clock is being recovered. */
char single_hop(int clock, btbb_piconet *pnet);

void try_hop(btbb_packet *pkt, btbb_piconet *pn);

#endif /* INCLUDED_BLUETOOTH_PICONET_H */
