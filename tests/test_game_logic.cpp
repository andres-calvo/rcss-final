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
    
    // NUEVO COMPORTAMIENTO: Con ángulo 45°, ahora usa dash direccional
    // porque la nueva lógica prioriza movimiento sobre giros
    sensors.ball = ObjectInfo(10.0f, 45.0f);  // Bola a 45 grados
    
    Action action = logic.decide_action(sensors);
    
    // Ahora debe hacer DASH direccional (no TURN) para ángulos <= 90°
    EXPECT_EQ(action.type, ActionType::DASH);
    EXPECT_FLOAT_EQ(action.params[0], 80.0f);  // Potencia reducida a 10m
    EXPECT_FLOAT_EQ(action.params[1], 45.0f);   // Dirección del dash
    
    // Verificación adicional: Con ángulo extremo (> 90°) también usa dash direccional
    logic.reset();
    sensors.ball = ObjectInfo(10.0f, 120.0f);  // Ángulo extremo
    action = logic.decide_action(sensors);
    EXPECT_EQ(action.type, ActionType::DASH);  // Dash direccional siempre
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

TEST_F(GameLogicTest, StrikerSearchesGoalWhenNotVisible) {
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);   // Bola en rango de pateo
    sensors.goal.visible = false;             // Arco NO visible
    
    Action action = logic.decide_action(sensors);
    
    // Debe GIRAR para buscar el arco, no patear ciegamente
    EXPECT_EQ(action.type, ActionType::TURN);
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

// =============================================================================
// Tests de Localización (Triangulación)
// =============================================================================

#include "localization.h"

TEST(LocalizationTest, ReturnsInvalidWithLessThanTwoFlags) {
    FlagInfo flags[1];
    flags[0] = FlagInfo("f c", 10.0f, 0.0f);
    
    PlayerPosition pos = Localization::estimate_position(flags, 1);
    
    EXPECT_FALSE(pos.valid);
}

TEST(LocalizationTest, CalculatesAngleToEnemyGoalFromCenter) {
    // Jugador en el centro del campo, mirando hacia la derecha (heading = 0)
    PlayerPosition pos(0.0f, 0.0f, 0.0f);
    pos.valid = true;
    
    float angle = Localization::angle_to_enemy_goal(pos);
    
    // Arco enemigo está al frente (x = 52.5, y = 0)
    EXPECT_NEAR(angle, 0.0f, 5.0f);
}

TEST(LocalizationTest, CalculatesAngleToEnemyGoalWhenFacingUp) {
    // Jugador en el centro, mirando hacia arriba (heading = 90)
    PlayerPosition pos(0.0f, 0.0f, 90.0f);
    pos.valid = true;
    
    float angle = Localization::angle_to_enemy_goal(pos);
    
    // Debe girar -90 grados para mirar hacia el arco
    EXPECT_NEAR(angle, -90.0f, 5.0f);
}

TEST(LocalizationTest, CalculatesAngleToEnemyGoalWhenFacingLeft) {
    // Jugador mirando hacia la izquierda (heading = 180)
    PlayerPosition pos(0.0f, 0.0f, 180.0f);
    pos.valid = true;
    
    float angle = Localization::angle_to_enemy_goal(pos);
    
    // Debe girar 180 grados (o -180) para mirar hacia el arco
    EXPECT_TRUE(std::abs(angle) > 170.0f);  // Cerca de 180
}

TEST(LocalizationTest, HandlesPositionNearGoal) {
    // Jugador cerca del arco enemigo
    PlayerPosition pos(40.0f, 10.0f, 45.0f);
    pos.valid = true;
    
    float angle = Localization::angle_to_enemy_goal(pos);
    
    // El ángulo debe ser válido (entre -180 y 180)
    EXPECT_TRUE(angle >= -180.0f && angle <= 180.0f);
}

TEST(LocalizationTest, StrikerUsesTriangulationWhenGoalNotVisible) {
    // Test que el striker usa la posición estimada cuando el arco no es visible
    GameLogic logic;
    SensorData sensors;
    
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    sensors.ball = ObjectInfo(0.5f, 0.0f);  // Bola en rango de pateo
    sensors.goal.visible = false;           // Arco NO visible
    
    // Simular posición estimada por triangulación
    sensors.position = PlayerPosition(-20.0f, 0.0f, 0.0f);  // Centro-izquierdo, mirando derecha
    sensors.position.valid = true;
    
    Action action = logic.decide_action(sensors);
    
    // Debe patear hacia el arco (no girar para buscarlo)
    EXPECT_EQ(action.type, ActionType::KICK);
    EXPECT_EQ(logic.get_state(), AgentState::DRIBBLING);
    // El ángulo debe ser cercano a 0 (hacia el arco que está adelante)
    EXPECT_NEAR(action.params[1], 0.0f, 15.0f);
}
