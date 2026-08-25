#include <gtest/gtest.h>

#define private public
#include "client/protocolgame.h"
#include "client/creature.h"
#undef private

#include "client/castprogressprotocol.h"
#include "client/game.h"
#include "client/map.h"

#include <framework/core/logger.h>
#include <framework/core/resourcemanager.h>
#include <framework/core/eventdispatcher.h>
#include <framework/graphics/texturemanager.h>
#include <framework/luaengine/luainterface.h>
#include <framework/ui/uiwidget.h>

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
        g_dispatcher.init();
        g_dispatcher.shutdown();
        g_lua.init();
        g_lua.registerClass<UIWidget>();
        g_textures.init();
    }

    void TearDown() override
    {
        g_textures.terminate();
        g_lua.terminate();
        g_resources.terminate();
        g_logger.setLevel(m_previousLogLevel);
    }

private:
    Fw::LogLevel m_previousLogLevel{ Fw::LogFatal };
};

[[maybe_unused]] testing::Environment* const g_frameworkEnv = testing::AddGlobalTestEnvironment(new FrameworkEnvironment);

constexpr auto startTime = CastProgressClock::time_point{} + std::chrono::seconds(10);

void appendU8(std::string& bytes, const uint8_t value)
{
    bytes.push_back(static_cast<char>(value));
}

void appendU32(std::string& bytes, const uint32_t value)
{
    for (uint8_t shift = 0; shift < 32; shift += 8)
        appendU8(bytes, static_cast<uint8_t>(value >> shift));
}

void appendU64(std::string& bytes, const uint64_t value)
{
    for (uint8_t shift = 0; shift < 64; shift += 8)
        appendU8(bytes, static_cast<uint8_t>(value >> shift));
}

InputMessagePtr makeInputMessage(const std::string& bytes)
{
    auto msg = std::make_shared<InputMessage>();
    msg->setBuffer(bytes);
    msg->setReadPos(g_game.getClientVersion() >= 1405 ? 7 : 8);
    return msg;
}

CreaturePtr registerCreature(const uint32_t id)
{
    auto creature = std::make_shared<Creature>();
    creature->setId(id);
    g_map.addCreature(creature);
    return creature;
}

void unregisterCreature(const uint32_t id)
{
    g_map.removeCreatureById(id);
}

struct CastProgressWidgetFixture
{
    CreaturePtr creature = std::make_shared<Creature>();
    UIWidgetPtr root = std::make_shared<UIWidget>();
    UIWidgetPtr bar = std::make_shared<UIWidget>();
    UIWidgetPtr track = std::make_shared<UIWidget>();
    UIWidgetPtr fill = std::make_shared<UIWidget>();

    CastProgressWidgetFixture()
    {
        bar->setId("castProgressBar");
        bar->resize(31, 4);
        track->setId("castProgressTrack");
        track->resize(29, 2);
        fill->setId("castProgressFill");
        fill->resize(0, 2);
        bar->addChild(track);
        bar->addChild(fill);
        root->addChild(bar);
        creature->setWidgetInformation(root);
    }

    ~CastProgressWidgetFixture()
    {
        creature->setWidgetInformation(nullptr);
    }
};

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

    ASSERT_TRUE(creature.getCastProgress(startTime).has_value());
    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_FALSE(creature.getCastProgress(startTime).has_value());
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

    const auto finalProgress = creature.getCastProgress(startTime + std::chrono::milliseconds(1000));
    ASSERT_TRUE(finalProgress.has_value());
    EXPECT_FLOAT_EQ(1.0F, *finalProgress);
    EXPECT_FALSE(creature.hasCastProgress());
    EXPECT_FALSE(creature.getCastProgress(startTime + std::chrono::milliseconds(1000)).has_value());
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

TEST(CastProgressProtocol, ReservesTheApprovedNumericContract)
{
    EXPECT_EQ(136u, CastProgressProtocol::Feature);
    EXPECT_EQ(136, Otc::GameCastProgress);
    EXPECT_EQ(15u, CastProgressProtocol::CreatureDataSubtype);
    EXPECT_EQ(1u, static_cast<uint8_t>(CastProgressProtocol::Action::Start));
    EXPECT_EQ(2u, static_cast<uint8_t>(CastProgressProtocol::Action::Cancel));
}

TEST(CastProgressProtocol, DecodesExactStartAndPreservesAlignment)
{
    std::string bytes;
    appendU8(bytes, 1);
    appendU64(bytes, 0x0102030405060708ULL);
    appendU32(bytes, 700);
    appendU32(bytes, 350);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);

    const auto command = CastProgressProtocol::readCommand(msg);

    EXPECT_EQ(CastProgressProtocol::Action::Start, command.action);
    EXPECT_EQ(0x0102030405060708ULL, command.id);
    EXPECT_EQ(700u, command.durationMs);
    EXPECT_EQ(350u, command.remainingMs);
    EXPECT_EQ(0xAAu, msg->getU8());
}

TEST(CastProgressProtocol, DecodesExactCancelAndPreservesAlignment)
{
    std::string bytes;
    appendU8(bytes, 2);
    appendU64(bytes, 0x0102030405060708ULL);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);

    const auto command = CastProgressProtocol::readCommand(msg);

    EXPECT_EQ(CastProgressProtocol::Action::Cancel, command.action);
    EXPECT_EQ(0x0102030405060708ULL, command.id);
    EXPECT_EQ(0xAAu, msg->getU8());
}

TEST(CastProgressProtocol, UnknownActionFailsDeterministically)
{
    const auto msg = makeInputMessage(std::string(1, static_cast<char>(3)));

    EXPECT_THROW(CastProgressProtocol::readCommand(msg), stdext::exception);
}

TEST(CastProgressProtocol, StandaloneStartMutatesAKnownCreatureAfterFullDecode)
{
    constexpr uint32_t creatureId = 5001;
    const auto creature = registerCreature(creatureId);
    std::string bytes;
    appendU32(bytes, creatureId);
    appendU8(bytes, CastProgressProtocol::CreatureDataSubtype);
    appendU8(bytes, 1);
    appendU64(bytes, 30);
    appendU32(bytes, 1000);
    appendU32(bytes, 500);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);
    ProtocolGame protocol;

    protocol.parseCreatureData(msg);

    EXPECT_EQ(30u, creature->getActiveCastProgressId());
    EXPECT_EQ(0xAAu, msg->getU8());
    unregisterCreature(creatureId);
}

TEST(CastProgressProtocol, UnknownCreatureConsumesACompleteStartWithoutOrphanState)
{
    std::string bytes;
    appendU32(bytes, 5999);
    appendU8(bytes, CastProgressProtocol::CreatureDataSubtype);
    appendU8(bytes, 1);
    appendU64(bytes, 31);
    appendU32(bytes, 1000);
    appendU32(bytes, 500);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);
    ProtocolGame protocol;

    protocol.parseCreatureData(msg);

    EXPECT_EQ(nullptr, g_map.getCreatureById(5999));
    EXPECT_EQ(0xAAu, msg->getU8());
}

TEST(CastProgressProtocol, StandaloneCancelRemovesOnlyTheMatchingActiveCast)
{
    constexpr uint32_t creatureId = 5005;
    const auto creature = registerCreature(creatureId);
    ASSERT_EQ(CastProgressApplyResult::Applied, creature->applyCastProgressStart(34, 1000, 1000, startTime));
    std::string bytes;
    appendU32(bytes, creatureId);
    appendU8(bytes, CastProgressProtocol::CreatureDataSubtype);
    appendU8(bytes, 2);
    appendU64(bytes, 34);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);
    ProtocolGame protocol;

    protocol.parseCreatureData(msg);

    EXPECT_FALSE(creature->hasCastProgress());
    EXPECT_EQ(0xAAu, msg->getU8());
    unregisterCreature(creatureId);
}

TEST(CastProgressProtocol, ZeroDurationIsConsumedAndIgnored)
{
    constexpr uint32_t creatureId = 5002;
    const auto creature = registerCreature(creatureId);
    std::string bytes;
    appendU32(bytes, creatureId);
    appendU8(bytes, CastProgressProtocol::CreatureDataSubtype);
    appendU8(bytes, 1);
    appendU64(bytes, 32);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);
    ProtocolGame protocol;

    protocol.parseCreatureData(msg);

    EXPECT_FALSE(creature->hasCastProgress());
    EXPECT_EQ(0xAAu, msg->getU8());
    unregisterCreature(creatureId);
}

TEST(CastProgressProtocol, OversizedRemainingIsClampedByCreatureState)
{
    constexpr uint32_t creatureId = 5003;
    const auto creature = registerCreature(creatureId);
    std::string bytes;
    appendU32(bytes, creatureId);
    appendU8(bytes, CastProgressProtocol::CreatureDataSubtype);
    appendU8(bytes, 1);
    appendU64(bytes, 33);
    appendU32(bytes, 1000);
    appendU32(bytes, 2000);
    const auto msg = makeInputMessage(bytes);
    ProtocolGame protocol;

    protocol.parseCreatureData(msg);

    const auto progress = creature->getCastProgress();
    ASSERT_TRUE(progress.has_value());
    EXPECT_GE(*progress, 0.0F);
    EXPECT_LT(*progress, 0.01F);
    unregisterCreature(creatureId);
}

TEST(CastProgressProtocol, StaleStartCannotReplaceNewerState)
{
    constexpr uint32_t creatureId = 5004;
    const auto creature = registerCreature(creatureId);
    ASSERT_EQ(CastProgressApplyResult::Applied, creature->applyCastProgressStart(40, 1000, 1000, startTime));
    std::string bytes;
    appendU32(bytes, creatureId);
    appendU8(bytes, CastProgressProtocol::CreatureDataSubtype);
    appendU8(bytes, 1);
    appendU64(bytes, 39);
    appendU32(bytes, 1000);
    appendU32(bytes, 500);
    const auto msg = makeInputMessage(bytes);
    ProtocolGame protocol;

    protocol.parseCreatureData(msg);

    EXPECT_EQ(40u, creature->getActiveCastProgressId());
    unregisterCreature(creatureId);
}

TEST(CastProgressProtocol, FeatureOffLeavesSnapshotTailUnread)
{
    const auto creature = std::make_shared<Creature>();
    const auto msg = makeInputMessage(std::string(1, static_cast<char>(0xAA)));

    CastProgressProtocol::parseSnapshotTail(msg, creature, false, startTime);

    EXPECT_EQ(0xAAu, msg->getU8());
    EXPECT_FALSE(creature->hasCastProgress());
}

TEST(CastProgressProtocol, ActiveSnapshotAppliesAndPreservesAlignment)
{
    const auto creature = std::make_shared<Creature>();
    std::string bytes;
    appendU8(bytes, 1);
    appendU64(bytes, 41);
    appendU32(bytes, 1000);
    appendU32(bytes, 500);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);

    CastProgressProtocol::parseSnapshotTail(msg, creature, true, startTime);

    EXPECT_EQ(41u, creature->getActiveCastProgressId());
    EXPECT_EQ(0xAAu, msg->getU8());
}

TEST(CastProgressProtocol, ActiveSnapshotForUnknownCreatureStillConsumesPayload)
{
    std::string bytes;
    appendU8(bytes, 1);
    appendU64(bytes, 42);
    appendU32(bytes, 1000);
    appendU32(bytes, 500);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);

    CastProgressProtocol::parseSnapshotTail(msg, nullptr, true, startTime);

    EXPECT_EQ(0xAAu, msg->getU8());
}

TEST(CastProgressProtocol, InactiveSnapshotClearsPresentationAndOrdering)
{
    const auto creature = std::make_shared<Creature>();
    ASSERT_EQ(CastProgressApplyResult::Applied, creature->applyCastProgressStart(43, 1000, 1000, startTime));
    std::string bytes;
    appendU8(bytes, 0);
    appendU8(bytes, 0xAA);
    const auto msg = makeInputMessage(bytes);

    CastProgressProtocol::parseSnapshotTail(msg, creature, true, startTime);

    EXPECT_FALSE(creature->hasCastProgress());
    EXPECT_EQ(CastProgressApplyResult::Applied, creature->applyCastProgressStart(43, 1000, 500, startTime));
    EXPECT_EQ(0xAAu, msg->getU8());
}

TEST(CastProgressProtocol, InvalidSnapshotMarkerFailsDeterministically)
{
    const auto creature = std::make_shared<Creature>();
    const auto msg = makeInputMessage(std::string(1, static_cast<char>(2)));

    EXPECT_THROW(CastProgressProtocol::parseSnapshotTail(msg, creature, true, startTime), stdext::exception);
}

TEST(CastProgressGeometry, UsesExactBackgroundAndTrackDimensions)
{
    const Rect nameRect(80, 100, 40, 12);

    const auto geometry = Creature::getCastProgressBarGeometry(nameRect, 0.5F);

    EXPECT_EQ(31, geometry.background.width());
    EXPECT_EQ(4, geometry.background.height());
    EXPECT_EQ(29, geometry.track.width());
    EXPECT_EQ(2, geometry.track.height());
}

TEST(CastProgressGeometry, CentersTheBarOverTheName)
{
    const Rect nameRect(80, 100, 40, 12);

    const auto geometry = Creature::getCastProgressBarGeometry(nameRect, 0.5F);

    EXPECT_EQ(nameRect.horizontalCenter(), geometry.background.horizontalCenter());
}

TEST(CastProgressGeometry, KeepsTheApprovedTwoPixelNameOffset)
{
    const Rect nameRect(80, 100, 40, 12);

    const auto geometry = Creature::getCastProgressBarGeometry(nameRect, 0.5F);

    EXPECT_EQ(nameRect.top() - 2, geometry.background.bottom());
}

TEST(CastProgressGeometry, InsetsTheTrackByOnePixel)
{
    const Rect nameRect(80, 100, 40, 12);

    const auto geometry = Creature::getCastProgressBarGeometry(nameRect, 0.5F);

    EXPECT_EQ(geometry.background.left() + 1, geometry.track.left());
    EXPECT_EQ(geometry.background.top() + 1, geometry.track.top());
}

TEST(CastProgressGeometry, ProducesZeroWidthAtZeroPercent)
{
    const auto geometry = Creature::getCastProgressBarGeometry(Rect(80, 100, 40, 12), 0.0F);

    EXPECT_EQ(0, geometry.fill.width());
    EXPECT_TRUE(geometry.fill.isEmpty());
}

TEST(CastProgressGeometry, FloorsHalfProgressToFourteenPixels)
{
    const auto geometry = Creature::getCastProgressBarGeometry(Rect(80, 100, 40, 12), 0.5F);

    EXPECT_EQ(14, geometry.fill.width());
    EXPECT_EQ(geometry.track.left(), geometry.fill.left());
}

TEST(CastProgressGeometry, ProducesFullWidthAtOneHundredPercent)
{
    const auto geometry = Creature::getCastProgressBarGeometry(Rect(80, 100, 40, 12), 1.0F);

    EXPECT_EQ(29, geometry.fill.width());
}

TEST(CastProgressGeometry, ClampsProgressOutsideTheValidRange)
{
    EXPECT_EQ(0, Creature::getCastProgressFillWidth(-1.0F));
    EXPECT_EQ(29, Creature::getCastProgressFillWidth(2.0F));
}

TEST(CastProgressGeometry, UsesTheApprovedExactColors)
{
    EXPECT_EQ(Color(0x00, 0x00, 0x00), Creature::getCastProgressBackgroundColor());
    EXPECT_EQ(Color(0x40, 0x40, 0x40), Creature::getCastProgressTrackColor());
    EXPECT_EQ(Color(0xFF, 0xFF, 0xFF), Creature::getCastProgressFillColor());
}

TEST(CastProgressGeometry, RemainsEligibleWithOnlyNamesEnabled)
{
    EXPECT_TRUE(Creature::shouldDrawCastProgress(Otc::DrawNames));
}

TEST(CastProgressGeometry, RemainsEligibleWithOnlyHealthBarsEnabled)
{
    EXPECT_TRUE(Creature::shouldDrawCastProgress(Otc::DrawBars));
}

TEST(CastProgressGeometry, IsHiddenWhenAllCreatureInformationIsDisabled)
{
    EXPECT_FALSE(Creature::shouldDrawCastProgress(Otc::DrawThings));
}

TEST(CastProgressWidget, CachesContainerAndFillWhenInformationWidgetIsAssigned)
{
    CastProgressWidgetFixture fixture;

    EXPECT_EQ(fixture.bar, fixture.creature->m_castProgressWidget);
    EXPECT_EQ(fixture.fill, fixture.creature->m_castProgressFillWidget);
}

TEST(CastProgressWidget, ClearsCachedReferencesWhenInformationWidgetIsRemoved)
{
    CastProgressWidgetFixture fixture;

    fixture.creature->setWidgetInformation(nullptr);

    EXPECT_FALSE(fixture.creature->m_castProgressWidget);
    EXPECT_FALSE(fixture.creature->m_castProgressFillWidget);
}

TEST(CastProgressWidget, HidesTheContainerWithoutAnActiveCast)
{
    CastProgressWidgetFixture fixture;

    fixture.creature->updateCastProgressWidget(Otc::DrawCreatureInfo, startTime);

    EXPECT_FALSE(fixture.bar->isExplicitlyVisible());
}

TEST(CastProgressWidget, ShowsZeroWhitePixelsAtCastStart)
{
    CastProgressWidgetFixture fixture;
    ASSERT_EQ(CastProgressApplyResult::Applied,
              fixture.creature->applyCastProgressStart(20, 1000, 1000, startTime));

    fixture.creature->updateCastProgressWidget(Otc::DrawCreatureInfo, startTime);

    EXPECT_TRUE(fixture.bar->isExplicitlyVisible());
    EXPECT_EQ(0, fixture.fill->getWidth());
}

TEST(CastProgressWidget, UsesTheSharedFourteenPixelWidthAtHalfProgress)
{
    CastProgressWidgetFixture fixture;
    ASSERT_EQ(CastProgressApplyResult::Applied,
              fixture.creature->applyCastProgressStart(21, 1000, 500, startTime));

    fixture.creature->updateCastProgressWidget(Otc::DrawCreatureInfo, startTime);

    EXPECT_TRUE(fixture.bar->isExplicitlyVisible());
    EXPECT_EQ(Creature::getCastProgressFillWidth(0.5F), fixture.fill->getWidth());
    EXPECT_EQ(14, fixture.fill->getWidth());
}

TEST(CastProgressWidget, DrawsFullWidthOnceThenHidesOnTheNextFrame)
{
    CastProgressWidgetFixture fixture;
    ASSERT_EQ(CastProgressApplyResult::Applied,
              fixture.creature->applyCastProgressStart(22, 1000, 1000, startTime));

    fixture.creature->updateCastProgressWidget(Otc::DrawCreatureInfo,
                                               startTime + std::chrono::milliseconds(1000));
    EXPECT_TRUE(fixture.bar->isExplicitlyVisible());
    EXPECT_EQ(29, fixture.fill->getWidth());

    fixture.creature->updateCastProgressWidget(Otc::DrawCreatureInfo,
                                               startTime + std::chrono::milliseconds(1001));
    EXPECT_FALSE(fixture.bar->isExplicitlyVisible());
}

TEST(CastProgressWidget, MissingFillKeepsAnIncompleteBarHidden)
{
    auto creature = std::make_shared<Creature>();
    auto root = std::make_shared<UIWidget>();
    auto bar = std::make_shared<UIWidget>();
    bar->setId("castProgressBar");
    root->addChild(bar);
    creature->setWidgetInformation(root);
    ASSERT_EQ(CastProgressApplyResult::Applied, creature->applyCastProgressStart(23, 1000, 1000, startTime));

    creature->updateCastProgressWidget(Otc::DrawCreatureInfo, startTime);

    EXPECT_FALSE(bar->isExplicitlyVisible());
    creature->setWidgetInformation(nullptr);
}
