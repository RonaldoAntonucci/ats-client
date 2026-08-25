#include <gtest/gtest.h>

#include "client/creature.h"

#include <framework/core/logger.h>
#include <framework/core/resourcemanager.h>
#include <framework/graphics/texturemanager.h>

namespace {

class FrameworkEnvironment : public testing::Environment
{
public:
    void SetUp() override
    {
        m_previousLogLevel = g_logger.getLevel();
        g_logger.setLevel(Fw::LogFatal);
        g_resources.init(".");
        g_resources.addSearchPath(".");
        g_textures.init();
    }

    void TearDown() override
    {
        g_textures.terminate();
        g_resources.terminate();
        g_logger.setLevel(m_previousLogLevel);
    }

private:
    Fw::LogLevel m_previousLogLevel{ Fw::LogFatal };
};

[[maybe_unused]] testing::Environment* const g_frameworkEnv = testing::AddGlobalTestEnvironment(new FrameworkEnvironment);

constexpr auto startTime = CastProgressClock::time_point{} + std::chrono::seconds(10);

} // namespace

TEST(CastProgressState, AcceptsAValidStartAtZeroProgress)
{
    Creature creature;

    EXPECT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(1, 1000, 1000, startTime));
    ASSERT_TRUE(creature.hasCastProgress());
    ASSERT_EQ(1u, creature.getActiveCastProgressId());
    ASSERT_TRUE(creature.getCastProgress(startTime).has_value());
    EXPECT_FLOAT_EQ(0.0F, *creature.getCastProgress(startTime));
}

TEST(CastProgressState, StartsALateSnapshotAtElapsedProgress)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(2, 1000, 750, startTime));

    const auto progress = creature.getCastProgress(startTime);

    ASSERT_TRUE(progress.has_value());
    EXPECT_FLOAT_EQ(0.25F, *progress);
}

TEST(CastProgressState, ClampsOversizedRemainingTimeToZeroProgress)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(3, 1000, 2000, startTime));

    const auto progress = creature.getCastProgress(startTime);

    ASSERT_TRUE(progress.has_value());
    EXPECT_FLOAT_EQ(0.0F, *progress);
}

TEST(CastProgressState, RemainingZeroExpiresOnTheNextFrame)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(4, 1000, 0, startTime));

    EXPECT_FALSE(creature.getCastProgress(startTime).has_value());
    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_EQ(CastProgressApplyResult::IgnoredStale, creature.applyCastProgressStart(4, 1000, 1000, startTime));
}

TEST(CastProgressState, ActiveDuplicateRefreshesTheSameState)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(5, 1000, 500, startTime));

    EXPECT_EQ(CastProgressApplyResult::Refreshed, creature.applyCastProgressStart(5, 1000, 1000, startTime));
    ASSERT_EQ(5u, creature.getActiveCastProgressId());
    ASSERT_TRUE(creature.getCastProgress(startTime).has_value());
    EXPECT_FLOAT_EQ(0.0F, *creature.getCastProgress(startTime));
}

TEST(CastProgressState, NewerIdReplacesTheActiveState)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(6, 1000, 500, startTime));

    EXPECT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(7, 2000, 2000, startTime));
    EXPECT_EQ(7u, creature.getActiveCastProgressId());
    ASSERT_TRUE(creature.getCastProgress(startTime).has_value());
    EXPECT_FLOAT_EQ(0.0F, *creature.getCastProgress(startTime));
}

TEST(CastProgressState, OlderIdCannotReplaceTheActiveState)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(9, 1000, 500, startTime));

    EXPECT_EQ(CastProgressApplyResult::IgnoredStale, creature.applyCastProgressStart(8, 1000, 1000, startTime));
    EXPECT_EQ(9u, creature.getActiveCastProgressId());
    ASSERT_TRUE(creature.getCastProgress(startTime).has_value());
    EXPECT_FLOAT_EQ(0.5F, *creature.getCastProgress(startTime));
}

TEST(CastProgressState, MatchingCancelClearsTheActiveStateImmediately)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(10, 1000, 1000, startTime));

    EXPECT_TRUE(creature.applyCastProgressCancel(10));
    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_FALSE(creature.getActiveCastProgressId().has_value());
}

TEST(CastProgressState, DifferentCancelLeavesTheActiveStateUntouched)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(11, 1000, 1000, startTime));

    EXPECT_FALSE(creature.applyCastProgressCancel(12));
    EXPECT_TRUE(creature.hasCastProgress());
    EXPECT_EQ(11u, creature.getActiveCastProgressId());
}

TEST(CastProgressState, ZeroDurationDoesNotMutateExistingState)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(13, 1000, 1000, startTime));

    EXPECT_EQ(CastProgressApplyResult::IgnoredInvalid, creature.applyCastProgressStart(14, 0, 0, startTime));
    EXPECT_EQ(13u, creature.getActiveCastProgressId());
}

TEST(CastProgressState, ZeroIdIsInvalidAndCreatesNoState)
{
    Creature creature;

    EXPECT_EQ(CastProgressApplyResult::IgnoredInvalid, creature.applyCastProgressStart(0, 1000, 1000, startTime));
    EXPECT_FALSE(creature.hasCastProgress());
}

TEST(CastProgressState, ExactDeadlineExpiresAndPreservesTheTombstone)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(15, 1000, 1000, startTime));

    EXPECT_FALSE(creature.getCastProgress(startTime + std::chrono::milliseconds(1000)).has_value());
    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_EQ(CastProgressApplyResult::IgnoredStale, creature.applyCastProgressStart(15, 1000, 500, startTime));
}

TEST(CastProgressState, ConfirmedDisappearanceClearsStateAndOrdering)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(16, 1000, 1000, startTime));

    creature.clearCastProgress(true);

    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(16, 1000, 500, startTime));
}

TEST(CastProgressState, OrdinaryClearKeepsOrderingTombstone)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(17, 1000, 1000, startTime));

    creature.clearCastProgress();

    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_EQ(CastProgressApplyResult::IgnoredStale, creature.applyCastProgressStart(17, 1000, 500, startTime));
}

TEST(CastProgressState, InactiveSnapshotClearsResidualOrdering)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(18, 1000, 1000, startTime));

    creature.applyCastProgressSnapshot(std::nullopt, startTime);

    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(18, 1000, 500, startTime));
}

TEST(CastProgressState, ActiveSnapshotIsAuthoritativeForANewPresentation)
{
    Creature creature;
    ASSERT_EQ(CastProgressApplyResult::Applied, creature.applyCastProgressStart(20, 1000, 1000, startTime));

    creature.applyCastProgressSnapshot(CastProgressWireState{ 19, 1000, 500 }, startTime);

    EXPECT_EQ(19u, creature.getActiveCastProgressId());
    ASSERT_TRUE(creature.getCastProgress(startTime).has_value());
    EXPECT_FLOAT_EQ(0.5F, *creature.getCastProgress(startTime));
}

TEST(CastProgressState, StateIsIsolatedPerCreature)
{
    Creature first;
    Creature second;
    ASSERT_EQ(CastProgressApplyResult::Applied, first.applyCastProgressStart(21, 1000, 1000, startTime));

    EXPECT_TRUE(first.hasCastProgress());
    EXPECT_FALSE(second.hasCastProgress());
    EXPECT_FALSE(second.applyCastProgressCancel(21));
    EXPECT_TRUE(first.hasCastProgress());
}
