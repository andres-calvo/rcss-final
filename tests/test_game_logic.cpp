/**
 * @file test_game_logic.cpp
 * @brief Tests unitarios para la lógica de juego C++.
 * 
 * Siguiendo TDD: verificamos que la función decide_action()
 * retorna los comandos correctos dado un estado de sensores.
 */

#include <gtest/gtest.h>
#include "game_logic.h"
#include "messages.h"

using namespace robocup;

// =============================================================================
// Tests de estructuras básicas
// =============================================================================

TEST(MessagesTest, ActionDefaultIsNone) {
    Action action;
    EXPECT_EQ(action.type, ActionType::NONE);
}

TEST(MessagesTest, ActionDashCreatesCorrectAction) {
    Action action = Action::dash(100, 30);
    EXPECT_EQ(action.type, ActionType::DASH);
    EXPECT_FLOAT_EQ(action.params[0], 100);
    EXPECT_FLOAT_EQ(action.params[1], 30);
}

TEST(MessagesTest, ActionKickCreatesCorrectAction) {
    Action action = Action::kick(80, -15);
    EXPECT_EQ(action.type, ActionType::KICK);
    EXPECT_FLOAT_EQ(action.params[0], 80);
    EXPECT_FLOAT_EQ(action.params[1], -15);
}

TEST(MessagesTest, ActionTurnCreatesCorrectAction) {
    Action action = Action::turn(45);
    EXPECT_EQ(action.type, ActionType::TURN);
    EXPECT_FLOAT_EQ(action.params[0], 45);
}

TEST(MessagesTest, SensorDataDefaultsToIdle) {
    SensorData sensors;
    EXPECT_EQ(sensors.status, GameStatus::IDLE);
}

// =============================================================================
// Tests de GameLogic - Estado IDLE
// =============================================================================

class GameLogicTest : public ::testing::Test {
protected:
    GameLogic logic;
    SensorData sensors;
    
    void SetUp() override {
        logic.reset();
        sensors = SensorData();
    }
};

TEST_F(GameLogicTest, ReturnsNoneWhenIdle) {
    sensors.status = GameStatus::IDLE;
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::NONE);
}

TEST_F(GameLogicTest, ReturnsNoneWhenFinished) {
    sensors.status = GameStatus::FINISHED;
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::NONE);
}

// =============================================================================
// Tests de STRIKER
// =============================================================================

TEST_F(GameLogicTest, StrikerSearchesBallWhenNotVisible) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball.visible = false;
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::TURN);
    EXPECT_EQ(logic.get_state(), AgentState::SEARCHING_BALL);
}

TEST_F(GameLogicTest, StrikerApproachesBallWhenFarAway) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball = ObjectInfo(10.0f, 0.0f);  // Bola visible a 10m, ángulo 0
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::DASH);
    EXPECT_EQ(logic.get_state(), AgentState::APPROACHING_BALL);
}

TEST_F(GameLogicTest, StrikerTurnsToBallWhenMisaligned) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball = ObjectInfo(10.0f, 45.0f);  // Bola a 45 grados
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::TURN);
    EXPECT_FLOAT_EQ(action.params[0], 45.0f);
}

TEST_F(GameLogicTest, StrikerShootsWhenCloseToGoal) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);   // Bola en rango de pateo
    sensors.goal = ObjectInfo(20.0f, 0.0f);  // Gol visible y cerca
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::KICK);
    EXPECT_EQ(logic.get_state(), AgentState::SHOOTING);
}

TEST_F(GameLogicTest, StrikerDribblesWhenGoalFar) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);   // Bola en rango
    sensors.goal = ObjectInfo(50.0f, 0.0f);  // Gol muy lejos
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::KICK);
    EXPECT_EQ(logic.get_state(), AgentState::DRIBBLING);
}

// =============================================================================
// Tests de GOALKEEPER
// =============================================================================

TEST_F(GameLogicTest, GoalkeeperCatchesWhenBallClose) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::GOALKEEPER;
    sensors.ball = ObjectInfo(1.5f, -20.0f);  // Bola cerca
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::CATCH);
    EXPECT_EQ(logic.get_state(), AgentState::CATCHING);
}

TEST_F(GameLogicTest, GoalkeeperStaysWhenBallNotVisible) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::GOALKEEPER;
    sensors.ball.visible = false;
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::NONE);
}

// =============================================================================
// Tests de DEFENDER
// =============================================================================

TEST_F(GameLogicTest, DefenderApproachesBall) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::DEFENDER;
    sensors.ball = ObjectInfo(15.0f, 10.0f);
    
    Action action = logic.decide_action(sensors);
    
    // Debe acercarse o girar hacia la bola
    EXPECT_TRUE(action.type == ActionType::DASH || action.type == ActionType::TURN);
}

TEST_F(GameLogicTest, DefenderClearsBallWhenClose) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::DEFENDER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);  // Bola en rango
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::KICK);
}

// =============================================================================
// Tests de PASSER
// =============================================================================

TEST_F(GameLogicTest, PasserPassesToTeammate) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::PASSER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);
    sensors.teammates[0] = TeammateInfo{2, 10.0f, 30.0f, true};
    sensors.teammate_count = 1;
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::KICK);
    EXPECT_EQ(logic.get_state(), AgentState::PASSING);
}

// =============================================================================
// Tests de DRIBBLER
// =============================================================================

TEST_F(GameLogicTest, DribblerDribblesForward) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::DRIBBLER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);
    
    Action action = logic.decide_action(sensors);
    
    EXPECT_EQ(action.type, ActionType::KICK);
    EXPECT_EQ(logic.get_state(), AgentState::DRIBBLING);
}
