#ifndef MCU_H
#define MCU_H

#include "mame_utils.h"
#include "rd_board.h"

/**
 * @file mcu.h
 * @brief El core HD63701 (derivado de MAME, trampa 5 de CLAUDE.md) y la placa a la que está soldado.
 */

/**
 * @brief La CPU: juego de instrucciones, temporizador y líneas de interrupción.
 *
 * El mapa de memoria vive en RdBoard. El protocolo del firmware se expone aquí
 * por intención —la cola de bytes es privada— porque boot() y setMasterTune()
 * tienen que correr la CPU entre mensajes.
 */
class Mcu : public RdBoardCpu
{
  public:
    Mcu(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_progrom, const u8 *temp_paramsrom);
    ~Mcu();

    /// 1,2 MB de estado: una copia accidental daría un emulador divergente en
    /// silencio.
    Mcu(const Mcu &) = delete;
    Mcu &operator=(const Mcu &) = delete;
    Mcu(Mcu &&) = delete;
    Mcu &operator=(Mcu &&) = delete;

    typedef void (Mcu::*op_func)();

    /** @brief Ejecuta una instrucción. */
    void execute_one();

    /**
     * @brief Fija el estado de una línea de interrupción y atiende lo que toque.
     * @param irqline Una de M6800_IRQ_LINE / M6801_TIN_LINE…
     * @param state ASSERT_LINE o CLEAR_LINE.
     */
    void execute_set_input(int irqline, int state);

    /** @brief Ejecuta instrucciones hasta que toca la siguiente muestra. */
    void execute_run();

    /**
     * @brief Corre la CPU lo que dura una muestra y devuelve lo que suene.
     * @param sampleRate32 true para el ritmo de los parches de 32 kHz (62 ciclos por muestra en vez de 100).
     * @return La muestra del chip de sonido.
     */
    s32 generate_next_sample(bool sampleRate32 = false);

    /**
     * @brief Handshake de arranque completo.
     *
     * Reset, parche 0, master tune, 1024 muestras de margen y recarga del parche.
     *
     * @param masterTune Afinación con la que arrancar.
     * @param warmupRate32 Ritmo de la CPU durante esas 1024 muestras. Sin valor
     *        por defecto a propósito: plugin y harness lo pasan distinto en los
     *        parches de 32 kHz (trampa 7 de CLAUDE.md) y eso tiene que verse en
     *        la llamada.
     */
    void boot(int16_t masterTune, bool warmupRate32);

    /** @brief 0x31 → 0x30: que el firmware relea la página recién mapeada. */
    void reloadPatch();

    /**
     * @brief Afina el instrumento entero.
     *
     * Lleva el "switcharoo" 0x30 → tuning → 0x30 porque afinar parches distintos
     * del 0 no funciona sin él, y corre el emulador ~200 muestras.
     *
     * @param tune Desviación con signo en el rango de un int16.
     */
    void setMasterTune(int16_t tune);

    /** @brief Pánico: pedal arriba y las 128 notas apagadas. */
    void allNotesOff();

    /**
     * @brief Traduce un mensaje MIDI al protocolo del firmware.
     * @param cmd Byte de estado MIDI.
     * @param data1 Primer byte de datos.
     * @param data2 Segundo byte de datos; se ignora en los mensajes de uno solo.
     */
    void sendMidiCmd(u8 cmd, u8 data1, u8 data2);

    /**
     * @brief Carga un juego de ROM entero (~2,9 ms; hay tres juegos).
     *
     * Las ROM tienen que sobrevivir al Mcu: se guarda el puntero de la params
     * para remapear la página en selectPatch().
     *
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     * @param temp_paramsrom ROM de parámetros sin descifrar.
     */
    void loadRomSet(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_paramsrom);

    /**
     * @brief Mapea la página de un parche (~0,03 ms; hay dieciséis).
     * @param from_addr Offset del parche dentro de la ROM de parámetros.
     */
    void selectPatch(size_t from_addr);

    /**
     * @brief Descifra un juego de ROM en la ranura que se le diga.
     *
     * Con las tres ranuras descifradas de antemano, cambiar de juego de ROM ya no
     * cuesta milisegundos y cabe en el hilo de audio.
     *
     * @param slot Ranura de destino.
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     */
    void decodeRomSet(unsigned slot, const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    /**
     * @brief Activa un juego de ROM ya descifrado, en O(1).
     * @param slot Ranura ya poblada por decodeRomSet().
     * @param temp_paramsrom ROM de parámetros del juego, sin descifrar.
     */
    void selectRomSet(unsigned slot, const u8 *temp_paramsrom);

    /**
     * @brief Mapea una página de parámetros ya descifrada: selectPatch() sin descifrar.
     * @param page Página de PARAMS_PAGE_BYTES.
     * @param from_addr Offset del parche, para el remapeo del banco.
     */
    void selectPatchPage(const u8 *page, size_t from_addr);

    /**
     * @brief loadRomSet() + selectPatch(): hace el trabajo caro siempre.
     * @param temp_ic5 ROM de onda IC5.
     * @param temp_ic6 ROM de onda IC6.
     * @param temp_ic7 ROM de onda IC7.
     * @param temp_paramsrom ROM de parámetros sin descifrar.
     * @param from_addr Offset del parche dentro de la ROM de parámetros.
     */
    void loadSounds(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_paramsrom,
                    size_t from_addr);

    /**
     * @brief Reinicia todo el estado: registros, temporizador y, vía la placa, RAM, latch, cola y partes.
     *
     * Lo que es configuración —las dos ROM, la página de params mapeada, las
     * tablas de onda descifradas— sobrevive (trampa 8 de CLAUDE.md).
     */
    void reset();

    u32 programCounter() const override { return m_pc.d; }
    void setInputLine(int line, int state) override { execute_set_input(line, state); }
    u8 readCpuRegister(u16 addr) override;
    void writeCpuRegister(u16 addr, u8 data) override;

  private:
    RdBoard board;

    /// `read_byte`/`write_byte` son los nombres que usan las macros RM/WM del
    /// core de MAME: reenvío de una línea para no tocar mcu_ops.h.
    u8 read_byte(u16 addr) { return board.read(addr); }
    void write_byte(u16 addr, u8 data) { board.write(addr, data); }

    void take_trap();
    void check_irq_lines();

    /// No-op: aquí no hay presupuesto de ciclos que agotar (ver FIRMWARE.md §4).
    /// Existe porque la llaman WAI y SLP en mcu_ops.h, que es código de MAME y no
    /// se reescribe (trampa 5 de CLAUDE.md).
    void eat_cycles() {}

    u32 RM16(u32 Addr);
    void WM16(u32 Addr, PAIR *p);
    void enter_interrupt(const char *message, u16 irq_vector);

    PAIR m_ppc = {};         ///< Contador de programa anterior.
    PAIR m_pc = {};          ///< Contador de programa.
    PAIR m_s = {};           ///< Puntero de pila.
    PAIR m_x = {};           ///< Registro índice.
    PAIR m_d = {};           ///< Acumuladores.
    PAIR m_ea = {};          ///< Dirección efectiva (variable temporal).
    u8 m_cc = 0;             ///< Códigos de condición.
    u8 m_wai_state = 0;      ///< Estado del opcode WAI (o del SLP).
    u8 m_nmi_state = 0;      ///< Estado de la línea NMI.
    u8 m_nmi_pending = 0;    ///< NMI pendiente.
    u8 m_irq_state[5] = {0}; ///< Estado de las líneas [IRQ1, TIN, IS3, …].

    u8 m_tcsr = 0;           ///< Timer Control and Status Register.
    PAIR m_counter = {};     ///< Contador libre.
    u8 m_pending_tcsr = 0;   ///< Flag de IRQ pendiente, para el borrado del flag.
    u16 m_input_capture = 0; ///< Captura de entrada.

    u8 tcsr_r();
    void tcsr_w(u8 data);

    static const u8 flags8i[256];
    static const u8 flags8d[256];
    enum
    {
        M6800_WAI = 8,   ///< WAI esperando una interrupción.
        M6800_SLP = 0x10 ///< Sólo HD63701.
    };

    /// Los ciclos por opcode del 63701. Nadie los suma hoy —el reloj maestro es
    /// el audio, no la CPU—; se conservan como referencia para quien retome el
    /// modelo de ciclos (FIRMWARE.md §4).
    static const u8 cycles_63701[256];

    static const op_func hd63701_insn[256];
    void aba();
    void abx();
    void adca_di();
    void adca_ex();
    void adca_im();
    void adca_ix();
    void adcb_di();
    void adcb_ex();
    void adcb_im();
    void adcb_ix();
    void adcx_im();
    void adda_di();
    void adda_ex();
    void adda_im();
    void adda_ix();
    void addb_di();
    void addb_ex();
    void addb_im();
    void addb_ix();
    void addd_di();
    void addd_ex();
    void addx_ex();
    void addd_im();
    void addd_ix();
    void aim_di();
    void aim_ix();
    void anda_di();
    void anda_ex();
    void anda_im();
    void anda_ix();
    void andb_di();
    void andb_ex();
    void andb_im();
    void andb_ix();
    void asl_ex();
    void asl_ix();
    void asla();
    void aslb();
    void asld();
    void asr_ex();
    void asr_ix();
    void asra();
    void asrb();
    void bcc();
    void bcs();
    void beq();
    void bge();
    void bgt();
    void bhi();
    void bita_di();
    void bita_ex();
    void bita_im();
    void bita_ix();
    void bitb_di();
    void bitb_ex();
    void bitb_im();
    void bitb_ix();
    void ble();
    void bls();
    void blt();
    void bmi();
    void bne();
    void bpl();
    void bra();
    void brn();
    void bsr();
    void bvc();
    void bvs();
    void cba();
    void clc();
    void cli();
    void clr_ex();
    void clr_ix();
    void clra();
    void clrb();
    void clv();
    void cmpa_di();
    void cmpa_ex();
    void cmpa_im();
    void cmpa_ix();
    void cmpb_di();
    void cmpb_ex();
    void cmpb_im();
    void cmpb_ix();
    void cmpx_di();
    void cmpx_ex();
    void cmpx_im();
    void cmpx_ix();
    void com_ex();
    void com_ix();
    void coma();
    void comb();
    void daa();
    void dec_ex();
    void dec_ix();
    void deca();
    void decb();
    void des();
    void dex();
    void eim_di();
    void eim_ix();
    void eora_di();
    void eora_ex();
    void eora_im();
    void eora_ix();
    void eorb_di();
    void eorb_ex();
    void eorb_im();
    void eorb_ix();
    void illegl1();
    void illegl2();
    void illegl3();
    void inc_ex();
    void inc_ix();
    void inca();
    void incb();
    void ins();
    void inx();
    void jmp_ex();
    void jmp_ix();
    void jsr_di();
    void jsr_ex();
    void jsr_ix();
    void lda_di();
    void lda_ex();
    void lda_im();
    void lda_ix();
    void ldb_di();
    void ldb_ex();
    void ldb_im();
    void ldb_ix();
    void ldd_di();
    void ldd_ex();
    void ldd_im();
    void ldd_ix();
    void lds_di();
    void lds_ex();
    void lds_im();
    void lds_ix();
    void ldx_di();
    void ldx_ex();
    void ldx_im();
    void ldx_ix();
    void lsr_ex();
    void lsr_ix();
    void lsra();
    void lsrb();
    void lsrd();
    void mul();
    void neg_ex();
    void neg_ix();
    void nega();
    void negb();
    void nop();
    void oim_di();
    void oim_ix();
    void ora_di();
    void ora_ex();
    void ora_im();
    void ora_ix();
    void orb_di();
    void orb_ex();
    void orb_im();
    void orb_ix();
    void psha();
    void pshb();
    void pshx();
    void pula();
    void pulb();
    void pulx();
    void rol_ex();
    void rol_ix();
    void rola();
    void rolb();
    void ror_ex();
    void ror_ix();
    void rora();
    void rorb();
    void rti();
    void rts();
    void sba();
    void sbca_di();
    void sbca_ex();
    void sbca_im();
    void sbca_ix();
    void sbcb_di();
    void sbcb_ex();
    void sbcb_im();
    void sbcb_ix();
    void sec();
    void sei();
    void sev();
    void slp();
    void sta_di();
    void sta_ex();
    void sta_im();
    void sta_ix();
    void stb_di();
    void stb_ex();
    void stb_im();
    void stb_ix();
    void std_di();
    void std_ex();
    void std_im();
    void std_ix();
    void sts_di();
    void sts_ex();
    void sts_im();
    void sts_ix();
    void stx_di();
    void stx_ex();
    void stx_im();
    void stx_ix();
    void suba_di();
    void suba_ex();
    void suba_im();
    void suba_ix();
    void subb_di();
    void subb_ex();
    void subb_im();
    void subb_ix();
    void subd_di();
    void subd_ex();
    void subd_im();
    void subd_ix();
    void swi();
    void tab();
    void tap();
    void tba();
    void tim_di();
    void tim_ix();
    void tpa();
    void tst_ex();
    void tst_ix();
    void tsta();
    void tstb();
    void tsx();
    void txs();
    void undoc1();
    void undoc2();
    void wai();
    void xgdx();
    void cpx_di();
    void cpx_ex();
    void cpx_im();
    void cpx_ix();
    void trap();
    void btst_ix();
    void stx_nsc();
};

#endif
