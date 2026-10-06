/**
 * \defgroup QMdmmCore QMdmm Core library
 *
 * The game rules engine. Pure logic: it knows how a game of QMdmm is played,
 * but not how players connect -- \ref QMdmmNetworking delegates every rule
 * decision to what lives here.
 *
 * @c Logic is the round state machine; drive it through its reply slots and it
 * emits the request and result signals that move the round along. It works on a
 * @c Room -- a set of @c Player objects plus a @c LogicConfiguration, which
 * holds the rules of the game (damage and HP ranges, the punish rule, the
 * LetMove toggle, ...) and serializes to JSON. @c Data carries the enums and
 * flags those types exchange, and @c Protocol the wire format: @c Packet plus
 * the @c RequestId and @c NotifyId enums.
 *
 * Nothing here is tied to a network, a user interface or a particular
 * executable: @c Logic only emits signals and accepts slot calls, so a game can
 * be driven with no connection at all. The transports live in
 * \ref QMdmmNetworking.
 */
