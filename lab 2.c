#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// ─────────────────────────────────────────────
// PARÁMETROS DEL JUEGO (ajusta a tu gusto)
// ─────────────────────────────────────────────
#define VEL_JUGADOR       5    // frames entre lecturas de botón
#define VEL_CAIDA_INICIAL 22   // frames entre cada paso de caída (menor = más rápido)
#define VEL_CAIDA_MINIMA  7    // límite máximo de dificultad
#define SPAWN_INICIAL     35   // frames entre cada nuevo objeto
#define SPAWN_MINIMO      12   // intervalo mínimo de spawn

// ─────────────────────────────────────────────
// HARDWARE 
// ─────────────────────────────────────────────
const gpio_num_t todas_las_filas[6] = {
    GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_16, GPIO_NUM_15, GPIO_NUM_13, GPIO_NUM_12
};
const gpio_num_t col_verdes[6] = {
    GPIO_NUM_23, GPIO_NUM_19, GPIO_NUM_5, GPIO_NUM_33, GPIO_NUM_25, GPIO_NUM_27
};
const gpio_num_t col_rojas[6] = {
    GPIO_NUM_22, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_32, GPIO_NUM_26, GPIO_NUM_14
};
#define BTN_LEFT    GPIO_NUM_34
#define BTN_RIGHT   GPIO_NUM_35

// ─────────────────────────────────────────────
// UTILIDADES
// ─────────────────────────────────────────────
void retardo_us(uint64_t us) {
    uint64_t inicio = esp_timer_get_time();
    while ((esp_timer_get_time() - inicio) < us) {}
}

// Generador pseudo-aleatorio (sin stdlib rand)
static uint32_t semilla = 73491;
uint32_t mi_rand() {
    semilla ^= semilla << 13;
    semilla ^= semilla >> 17;
    semilla ^= semilla << 5;
    return semilla;
}

// ─────────────────────────────────────────────
// PANTALLA VIRTUAL
// ─────────────────────────────────────────────
// 0 = apagado | 1 = verde (jugador) | 2 = rojo (objeto)
uint8_t pantalla[6][6];

void limpiar_pantalla() {
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 6; c++)
            pantalla[r][c] = 0;
}

void multiplexar() {
    for (int r = 0; r < 6; r++) {
        // Apagar todo
        for (int i = 0; i < 6; i++) {
            gpio_set_level(todas_las_filas[i], 0);
            gpio_set_level(col_verdes[i], 0);
            gpio_set_level(col_rojas[i], 0);
        }
        // Encender columnas según pantalla virtual
        for (int c = 0; c < 6; c++) {
            if      (pantalla[r][c] == 1) gpio_set_level(col_verdes[c], 1);
            else if (pantalla[r][c] == 2) gpio_set_level(col_rojas[c],  1);
        }
        gpio_set_level(todas_las_filas[r], 1);
        retardo_us(2000);
        gpio_set_level(todas_las_filas[r], 0);
    }
}

// ─────────────────────────────────────────────
// ANIMACIONES DE FIN
// ─────────────────────────────────────────────
void animacion_game_over() {
    // Pantalla roja total durante ~1 segundo (85 barridos)
    for (int f = 0; f < 85; f++) {
        for (int r = 0; r < 6; r++) {
            for (int i = 0; i < 6; i++) {
                gpio_set_level(todas_las_filas[i], 0);
                gpio_set_level(col_verdes[i], 0);
                gpio_set_level(col_rojas[i], 0);
            }
            for (int c = 0; c < 6; c++) gpio_set_level(col_rojas[c], 1);
            gpio_set_level(todas_las_filas[r], 1);
            retardo_us(2000);
            gpio_set_level(todas_las_filas[r], 0);
        }
    }
}

// ─────────────────────────────────────────────
// APP MAIN
// ─────────────────────────────────────────────
void app_main(void) {

    // --- Setup GPIO ---
    for (int i = 0; i < 6; i++) {
        gpio_reset_pin(todas_las_filas[i]);
        gpio_set_direction(todas_las_filas[i], GPIO_MODE_OUTPUT);
        gpio_set_level(todas_las_filas[i], 0);

        gpio_reset_pin(col_verdes[i]);
        gpio_set_direction(col_verdes[i], GPIO_MODE_OUTPUT);
        gpio_set_level(col_verdes[i], 0);

        gpio_reset_pin(col_rojas[i]);
        gpio_set_direction(col_rojas[i], GPIO_MODE_OUTPUT);
        gpio_set_level(col_rojas[i], 0);
    }
    gpio_reset_pin(BTN_LEFT);
    gpio_set_direction(BTN_LEFT, GPIO_MODE_INPUT);
    gpio_reset_pin(BTN_RIGHT);
    gpio_set_direction(BTN_RIGHT, GPIO_MODE_INPUT);

    // --- Estado inicial ---
    int jugador_x     = 2;
    int ultimo_izq    = 1, ultimo_der = 1;
    int contador      = 0;

    // Objetos cayendo: objetos[c] = fila actual del objeto en columna c (-1 = vacío)
    int objetos[6];
    for (int i = 0; i < 6; i++) objetos[i] = -1;

    int vel_caida      = VEL_CAIDA_INICIAL;
    int spawn_intervalo = SPAWN_INICIAL;
    int spawn_timer    = 0;
    bool iniciado      = false;

    // ─────────────────────────────────────────
    // LOOP PRINCIPAL
    // ─────────────────────────────────────────
    while (1) {

        // ── A. LEER BOTONES ──
        int e_der = gpio_get_level(BTN_RIGHT);
        int e_izq = gpio_get_level(BTN_LEFT);

        if (contador % VEL_JUGADOR == 0) {
            // Cualquier pulsación nueva inicia el juego
            if ((e_der == 0 && ultimo_der == 1) || (e_izq == 0 && ultimo_izq == 1)) {
                iniciado = true;
            }
            if (e_der == 0 && ultimo_der == 1 && jugador_x < 5) jugador_x++;
            if (e_izq == 0 && ultimo_izq == 1 && jugador_x > 0) jugador_x--;

            ultimo_izq = e_izq;
            ultimo_der = e_der;
        }

        // ── B. LÓGICA DE JUEGO (solo si inició) ──
        if (iniciado) {

            // B1. Spawn de nuevos objetos
            spawn_timer++;
            if (spawn_timer >= spawn_intervalo) {
                spawn_timer = 0;

                // Elegir columna aleatoria vacía en la fila 0
                // Intentamos hasta 6 veces para no bloquear
                for (int intento = 0; intento < 6; intento++) {
                    int col = mi_rand() % 6;
                    if (objetos[col] == -1) {
                        objetos[col] = 0; // nace en la fila superior
                        break;
                    }
                }

                // Aumentar dificultad progresivamente
                if (vel_caida    > VEL_CAIDA_MINIMA) vel_caida--;
                if (spawn_intervalo > SPAWN_MINIMO)   spawn_intervalo--;
            }

            // B2. Mover objetos hacia abajo
            if (contador % vel_caida == 0) {
                bool game_over = false;

                for (int c = 0; c < 6; c++) {
                    if (objetos[c] == -1) continue;

                    objetos[c]++; // bajar una fila

                    if (objetos[c] == 5) {
                        // Llegó a la fila del jugador — ¿colisión?
                        if (c == jugador_x) {
                            game_over = true;
                        }
                        objetos[c] = -1; // salió de pantalla
                    }
                }

                // ── GAME OVER ──
                if (game_over) {
                    animacion_game_over();

                    // Resetear todo
                    jugador_x      = 2;
                    vel_caida      = VEL_CAIDA_INICIAL;
                    spawn_intervalo = SPAWN_INICIAL;
                    spawn_timer    = 0;
                    iniciado       = false;
                    contador       = 0;
                    for (int i = 0; i < 6; i++) objetos[i] = -1;

                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }
            }
        }

        // ── C. DIBUJAR ──
        limpiar_pantalla();

        // Jugador verde en fila 5
        pantalla[5][jugador_x] = 1;

        // Objetos rojos cayendo (filas 0–4)
        if (iniciado) {
            for (int c = 0; c < 6; c++) {
                if (objetos[c] >= 0 && objetos[c] <= 4) {
                    pantalla[objetos[c]][c] = 2;
                }
            }
        }

        // ── D. MULTIPLEXAR ──
        multiplexar();

        contador++;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}