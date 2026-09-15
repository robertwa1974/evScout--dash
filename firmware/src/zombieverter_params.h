// ZombieVerterDial Dial Display -- Parameter ID reference
// Auto-generated from params.json for porting to non-M5Stack targets (e.g. Waveshare)
// SDO param IDs = openinverter/ZombieVerter object dictionary IDs (read/write as float via SDO)
// CAN broadcast params = decoded directly from periodic CAN frames, no SDO needed

#pragma once

// ---------------------------------------------------------------
// CAN-BROADCAST TELEMETRY (decode directly from bus, no SDO poll)
// raw_value = (int16_t / uint16_t bits extracted at offset)
// scaled_value = raw_value * gain
// NOTE: byteOffset below is actually a BIT offset (kept consistent with
// params.json's canoffset convention) - divide by 8 for byte position.
// All observed offsets are byte-aligned (multiples of 8).
// ---------------------------------------------------------------
struct CanBroadcastParam {
    const char* name;
    uint16_t    paramId;    // SDO/telemetry param ID
    uint16_t    canId;      // CAN identifier (decimal)
    uint8_t     byteOffset; // BIT offset within frame (divide by 8 for byte offset)
    uint8_t     bitLength;  // 8 or 16
    bool        isSigned;   // sign-extend the extracted field before applying gain
    float       gain;       // multiply raw by this
    const char* unit;
};

// isSigned / gain below are BEST GUESSES pending a real bus capture (see
// firmware/can-bench.md). Rationale for the signed ones:
//   speed - motor RPM goes negative in reverse (Prius MG shafts especially)
//   idc   - pack current is negative on regen and while charging
//   tmpm/tmphs - can read a few degrees below zero cold-soaked
// The 0.09 gains look like a truncated repeating decimal in the source
// params.json; confirm the exact factor against SavvyCAN + the VCU web UI.
static const CanBroadcastParam kCanBroadcastParams[] = {
    {"tmphs", 2028, 0x126 /*294*/, 32, 16, true,  1.00f, "°C"},
    {"U12V", 2070, 0x210 /*528*/, 32, 16, false, 0.09f, "V"},
    {"speed", 2016, 0x257 /*599*/, 0, 16, true,  0.09f, "rpm"},   // motor RPM, NOT vehicle speed
    {"Gear", 27, 0x300 /*768*/, 0, 8, false, 1.00f, "0=LOW, 1=HIGH, 2=AUTO, 3=HIGHFWDLOWREV"},
    {"MotActive", 129, 0x301 /*769*/, 0, 8, false, 1.00f, "0=Mg1and2, 1=Mg1, 2=Mg2, 3=BlendingMG2and1"},
    {"regenmax", 61, 0x302 /*770*/, 0, 16, false, 1.00f, "%"},
    {"SOC", 2015, 0x355 /*853*/, 0, 16, false, 1.00f, "%"},
    {"udc", 2006, 0x356 /*854*/, 0, 16, false, 0.09f, "V"},
    {"idc", 2012, 0x356 /*854*/, 16, 16, true,  0.09f, "A"},
    {"tmpm", 2029, 0x356 /*854*/, 32, 16, true,  1.00f, "°C"},
};
static const size_t kCanBroadcastParamsCount = sizeof(kCanBroadcastParams) / sizeof(kCanBroadcastParams[0]);

// ---------------------------------------------------------------
// SDO-WRITABLE / READABLE PARAMETERS (settable via SDO object ID)
// isparam:true in params.json -- these are the ZombieVerter VCU's
// tunable settings, not raw CAN-scaled telemetry.
// ---------------------------------------------------------------
struct SdoParam {
    const char* name;
    uint16_t    paramId; // SDO object ID
    const char* unit;
};

static const SdoParam kSdoParams[] = {
    {"Inverter", 5, "0=None, 1=Leaf_Gen1, 2=GS450H, 3=UserCAN, 4=OpenI, 5=Prius_Gen3, 6=Outlander, 7=GS300H, 8=RearOutlander"},
    {"Vehicle", 6, "0=BMW_E46, 1=BMW_E6x+, 2=Classic, 3=None, 5=BMW_E39, 6=VAG, 7=Subaru, 8=BMW_E31"},
    {"potmin", 7, "dig"},
    {"potmax", 8, "dig"},
    {"pot2min", 9, "dig"},
    {"pot2max", 10, "dig"},
    {"potmode", 11, "0=SingleChannel, 1=DualChannel"},
    {"dirmode", 12, "0=Button, 1=Switch, 2=ButtonReversed, 3=SwitchReversed, 4=DefaultForward"},
    {"throtramp", 13, "%/10ms"},
    {"throtramprpm", 14, "rpm"},
    {"revlim", 15, "rpm"},
    {"udcmin", 19, "V"},
    {"udclim", 20, "V"},
    {"idcmax", 21, "A"},
    {"idcmin", 22, "A"},
    {"tmphsmax", 23, "°C"},
    {"tmpmmax", 24, "°C"},
    {"throtmax", 25, "%"},
    {"throtmin", 26, "%"},
    {"Gear", 27, "0=LOW, 1=HIGH, 2=AUTO, 3=HIGHFWDLOWREV"},
    {"OilPump", 28, "%"},
    {"cruisestep", 29, "rpm"},
    {"cruiseramp", 30, "rpm/100ms"},
    {"regenlevel", 31, ""},
    {"udcsw", 32, "V"},
    {"cruiselight", 33, "0=Off, 1=On, 2=na"},
    {"errlights", 34, "0=Off, 4=EPC, 8=engine"},
    {"chargemodes", 37, "0=Off, 1=EXT_DIGI, 2=Volt_Ampera, 3=Leaf_PDM, 4=TeslaOI, 5=Out_lander, 6=Elcon"},
    {"BattCap", 38, "kWh"},
    {"interface", 39, "0=Unused, 1=i3LIM, 2=Chademo, 3=CPC, 4=Foccci"},
    {"Voltspnt", 40, "V"},
    {"Pwrspnt", 41, "W"},
    {"CCS_ICmd", 42, "A"},
    {"CCS_ILim", 43, "A"},
    {"CCS_SOCLim", 44, "%"},
    {"Chgctrl", 45, "0=Enable, 1=Disable, 2=Timer"},
    {"Set_Day", 46, "0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat"},
    {"Set_Hour", 47, "Hours"},
    {"Set_Min", 48, "Mins"},
    {"Set_Sec", 49, "Secs"},
    {"Chg_Hrs", 50, "Hours"},
    {"Chg_Min", 51, "Mins"},
    {"Chg_Dur", 52, "Mins"},
    {"Pre_Hrs", 53, "Hours"},
    {"Pre_Min", 54, "Mins"},
    {"Pre_Dur", 55, "Mins"},
    {"IdcTerm", 56, "A"},
    {"Heater", 57, "0=None, 1=Ampera, 2=VW, 3=OutlanderCan"},
    {"Control", 58, "0=Disable, 1=Enable, 2=Timer"},
    {"HeatPwr", 59, "W"},
    {"regenrpm", 60, "rpm"},
    {"regenmax", 61, "%"},
    {"regenramp", 68, "%/10ms"},
    {"InverterCan", 70, "0=CAN1, 1=CAN2"},
    {"VehicleCan", 71, "0=CAN1, 1=CAN2"},
    {"ShuntCan", 72, "0=CAN1, 1=CAN2"},
    {"LimCan", 73, "0=CAN1, 1=CAN2"},
    {"ChargerCan", 74, "0=CAN1, 1=CAN2"},
    {"IsaInit", 75, "0=Off, 1=On, 2=na"},
    {"throtdead", 76, "%"},
    {"CAN3Speed", 77, "0=k33.3, 1=k500, 2=k100"},
    {"Transmission", 78, "0=Manual, 1=Auto"},
    {"SOCFC", 79, "%"},
    {"Out1Func", 80, "func code - see openinverter docs"},
    {"Out2Func", 81, "func code - see openinverter docs"},
    {"Out3Func", 82, "func code - see openinverter docs"},
    {"SL1Func", 83, "func code - see openinverter docs"},
    {"SL2Func", 84, "func code - see openinverter docs"},
    {"PWM1Func", 85, "func code - see openinverter docs"},
    {"PWM2Func", 86, "func code - see openinverter docs"},
    {"PWM3Func", 87, "func code - see openinverter docs"},
    {"ShuntType", 88, "0=None, 1=ISA, 2=SBOX, 3=VAG"},
    {"BMSCan", 89, "0=CAN1, 1=CAN2"},
    {"BMS_Mode", 90, "0=Off, 1=SimpBMS, 2=TiDaisychainSingle, 3=TiDaisychainDual, 4=LeafBms, 5=RenaultKangoo33"},
    {"BMS_Timeout", 91, "sec"},
    {"BMS_VminLimit", 92, "V"},
    {"BMS_VmaxLimit", 93, "V"},
    {"BMS_TminLimit", 94, "°C"},
    {"BMS_TmaxLimit", 95, "°C"},
    {"OBD2Can", 96, "0=CAN1, 1=CAN2"},
    {"CanMapCan", 97, "0=CAN1, 1=CAN2"},
    {"GP12VInFunc", 98, "func code - see openinverter docs"},
    {"HVReqFunc", 99, "func code - see openinverter docs"},
    {"Tim3_Presc", 100, ""},
    {"Tim3_Period", 101, ""},
    {"Tim3_1_OC", 102, ""},
    {"Tim3_2_OC", 103, ""},
    {"Tim3_3_OC", 104, ""},
    {"DCdc_Type", 105, "0=NoDCDC, 1=TeslaG2"},
    {"DCSetPnt", 106, "V"},
    {"DCDCCan", 107, "0=CAN1, 1=CAN2"},
    {"GearLvr", 108, "0=None, 1=BMW_F30, 2=JLR_G1, 3=JLR_G2, 4=BMW_E65"},
    {"GPA1Func", 110, "0=None, 1=ProxPilot, 2=BrakeVacSensor"},
    {"GPA2Func", 111, "0=None, 1=ProxPilot, 2=BrakeVacSensor"},
    {"ppthresh", 114, "dig"},
    {"BrkVacThresh", 115, "dig"},
    {"BrkVacHyst", 116, "dig"},
    {"DigiPot1Step", 117, "dig"},
    {"DigiPot2Step", 118, "dig"},
    {"ChgAcVolt", 120, "Vac"},
    {"ChgEff", 121, "%"},
    {"regenBrake", 122, "%"},
    {"throtmaxRev", 123, "%"},
    {"HeatPercnt", 124, "%"},
    {"regenendrpm", 126, "rpm"},
    {"reversemotor", 127, "0=Off, 1=On, 2=na"},
    {"RegenBrakeLight", 128, "%"},
    {"MotActive", 129, "0=Mg1and2, 1=Mg1, 2=Mg2, 3=BlendingMG2and1"},
    {"throtrpmfilt", 131, "rpm/10ms"},
    {"CP_PWM", 132, ""},
    {"ConfigFoccci", 133, "0=Off, 1=On, 2=na"},
    {"FanTemp", 134, "°C"},
    {"PumpPWM", 135, "0=GS450hOil, 1=TachoOut"},
    {"TachoPPR", 136, "PPR"},
    {"revRegen", 137, "0=Off, 1=On, 2=na"},
    {"HeaterCan", 138, "0=CAN1, 1=CAN2"},
    {"PB1InFunc", 140, "func code - see openinverter docs"},
    {"PB2InFunc", 141, "func code - see openinverter docs"},
    {"PB3InFunc", 142, "func code - see openinverter docs"},
};
static const size_t kSdoParamsCount = sizeof(kSdoParams) / sizeof(kSdoParams[0]);

// ---------------------------------------------------------------
// READ-ONLY TELEMETRY / STATUS (isparam:false, no CAN broadcast entry)
// Must be polled via SDO read request; no automatic frame to decode.
// ---------------------------------------------------------------
struct TelemetryParam {
    const char* name;
    uint16_t    paramId;
    const char* unit;
};

static const TelemetryParam kTelemetryParams[] = {
    {"version", 2000, "4=2.30.A"},
    {"opmode", 2002, "0=Off, 1=Run, 2=Precharge, 3=PchFail, 4=Charge"},
    {"chgtyp", 2003, "0=Off, 1=AC, 2=DCFC"},
    {"lasterr", 2004, "0=NONE, 1=BMSCOMM, 2=OVERVOLTAGE, 3=PRECHARGE, 4=THROTTLE1, 5=THROTTLE2, 6=THROTTLE12, 7=THROTTLE12DIFF, 8=THROTTLEMODE, 9=CANTIMEOUT, 10=TMPHSMAX, 11=TMPMMAX"},
    {"status", 2005, "0=None, 1=UdcLow, 2=UdcHigh, 4=UdcBelowUdcSw, 8=UdcLim, 16=EmcyStop, 32=MProt, 64=PotPressed, 128=TmpHs, 256=WaitStart"},
    {"udc2", 2007, "V"},
    {"udc3", 2008, "V"},
    {"deltaV", 2009, "V"},
    {"INVudc", 2010, "V"},
    {"power", 2011, "kW"},
    {"KWh", 2013, "kwh"},
    {"AMPh", 2014, "Ah"},
    {"Veh_Speed", 2017, "kph"},   // actual vehicle speed - not the broadcast "speed" field (that's motor rpm)
    {"torque", 2018, "dig"},
    {"pot", 2019, "dig"},
    {"pot2", 2020, "dig"},
    {"potbrake", 2021, "dig"},
    {"brakepressure", 2022, "dig"},
    {"potnom", 2023, "%"},
    {"dir", 2024, "-1=Reverse, 0=Neutral, 1=Drive, 2=Park"},
    {"tmpaux", 2030, "°C"},
    {"uaux", 2031, "V"},
    {"canio", 2032, "1=Cruise, 2=Start, 4=Brake, 8=Fwd, 16=Rev, 32=Bms"},
    {"cruisespeed", 2033, "rpm"},
    {"cruisestt", 2034, "0=None, 1=On, 2=Disable, 4=Set, 8=Resume"},
    {"din_cruise", 2035, "0=Off, 1=On, 2=na"},
    {"din_start", 2036, "0=Off, 1=On, 2=na"},
    {"din_brake", 2037, "0=Off, 1=On, 2=na"},
    {"din_forward", 2038, "0=Off, 1=On, 2=na"},
    {"din_reverse", 2039, "0=Off, 1=On, 2=na"},
    {"din_bms", 2040, "0=Off, 1=On, 2=na"},
    {"handbrk", 2041, "0=Off, 1=On, 2=na"},
    {"Gear1", 2042, "0=Off, 1=On, 2=na"},
    {"Gear2", 2043, "0=Off, 1=On, 2=na"},
    {"Gear3", 2044, "0=Off, 1=On, 2=na"},
    {"T15Stat", 2045, "0=Off, 1=On, 2=na"},
    {"InvStat", 2046, "0=Off, 1=On, 2=na"},
    {"GearFB", 2047, "0=LOW, 1=HIGH, 2=AUTO, 3=HIGHFWDLOWREV"},
    {"CableLim", 2048, "A"},
    {"PilotLim", 2049, "A"},
    {"PlugDet", 2050, "0=Off, 1=On, 2=na"},
    {"PilotTyp", 2051, "0=Absent, 1=ACStd, 2=ACchg, 3=Error, 4=CCS_Not_Rdy, 5=CCS_Rdy, 6=Static"},
    {"CCS_I_Avail", 2052, "A"},
    {"CCS_V_Avail", 2053, "V"},
    {"CCS_I", 2054, "A"},
    {"CCS_V", 2055, "V"},
    {"CCS_V_Min", 2056, "V"},
    {"CCS_V_Con", 2057, "V"},
    {"hvChg", 2058, "0=Off, 1=On, 2=na"},
    {"CCS_COND", 2059, "0=NotRdy, 1=ready, 2=SWoff, 3=interruption, 4=Prech, 5=insulmon, 6=estop, 7=malfunction, 15=invalid"},
    {"CCS_State", 2060, "s"},
    {"CP_DOOR", 2061, "0=CLOSED, 1=OPEN, 2=ERROR, 3=INVALID"},
    {"CCS_Contactor", 2062, "0=Off, 1=On, 2=na"},
    {"Day", 2064, "0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat"},
    {"Hour", 2065, "H"},
    {"Min", 2066, "M"},
    {"Sec", 2067, "S"},
    {"CCS_Ireq", 2068, "A"},
    {"HeatReq", 2069, "0=Off, 1=On, 2=na"},
    {"din_12Vgp", 2071, "0=Off, 1=On, 2=na"},
    {"ChgTemp", 2078, "°C"},
    {"AC_Volts", 2079, "V"},
    {"FrontRearBal", 2082, "%"},
    {"I12V", 2083, "A"},
    {"BMS_Vmin", 2084, "V"},
    {"BMS_Vmax", 2085, "V"},
    {"BMS_Tmin", 2086, "°C"},
    {"BMS_Tmax", 2087, "°C"},
    {"BMS_ChargeLim", 2088, "A"},
    {"ChgT", 2090, "M"},
    {"AC_Volts2", 2094, "dig"},
    {"BrkVacVal", 2095, "dig"},
    {"tmpheater", 2096, "°C"},
    {"udcheater", 2097, "V"},
    {"powerheater", 2098, "W"},
    {"BMS_IsoMeas", 2099, "mV"},
    {"VehLockSt", 2100, "0=Off, 1=On, 2=na"},
    {"BMS_MaxCharge", 2101, "W"},
    {"TorqDerate", 2102, "0=None, 1=UDClimLow, 2=UDClimHigh, 4=IDClimLow, 8=IDClimHigh, 16=TempLim"},
    {"BMS_Tavg", 2103, "°C"},
    {"BMS_Isolation", 2104, "Ohm"},
    {"BMS_MaxInput", 2105, "kW"},
    {"BMS_MaxOutput", 2106, "kW"},
    {"CanAct", 2107, "0=Off, 1=On, 2=na"},
};
static const size_t kTelemetryParamsCount = sizeof(kTelemetryParams) / sizeof(kTelemetryParams[0]);
