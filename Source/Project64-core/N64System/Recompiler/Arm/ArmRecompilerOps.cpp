/****************************************************************************
*                                                                           *
* Project64 - A Nintendo 64 emulator.                                      *
* http://www.pj64-emu.com/                                                  *
* Copyright (C) 2012 Project64. All rights reserved.                        *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/
#include "stdafx.h"

#if defined(__arm__) || defined(_M_ARM)
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/N64System/Mips/OpcodeName.h>
#include <Project64-core/N64System/Mips/MemoryVirtualMem.h>
#include <Project64-core/N64System/Interpreter/InterpreterOps32.h>
#include <Project64-core/N64System/Interpreter/InterpreterCPU.h>
#include <Project64-core/N64System/Recompiler/RecompilerCodeLog.h>
#include <Project64-core/N64System/Recompiler/Arm/ArmRecompilerOps.h>
#include <Project64-core/N64System/N64Class.h>
#include <Project64-core/ExceptionHandler.h>

void CArmRecompilerOps::PreCompileOpcode(void)
{
    if (m_NextInstruction != DELAY_SLOT_DONE)
    {
        CPU_Message("  %X %s", m_CompilePC, R4300iOpcodeName(m_Opcode.Hex, m_CompilePC));
    }

    /*if (m_CompilePC == 0x80000138 && m_NextInstruction == NORMAL)
    {
    m_RegWorkingSet.WriteBackRegisters();
    UpdateCounters(m_RegWorkingSet,false,true);
    MoveConstToVariable(m_CompilePC, &g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER");
    if (g_SyncSystem)
    {
    MoveConstToArmReg((uint32_t)g_BaseSystem, Arm_R0, "g_BaseSystem");
    CallFunction(AddressOf(&CN64System::SyncSystem), "CN64System::SyncSystem");
    }
    }*/
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_System->CountPerOp());
}

void CArmRecompilerOps::PostCompileOpcode ( void )
{
    if (!g_System->bRegCaching())
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

CArmRecompilerOps::CArmRecompilerOps() :
m_NextInstruction(NORMAL)
{
    memset(&m_Opcode,0,sizeof(m_Opcode));
}

bool DelaySlotEffectsCompare(uint32_t PC, uint32_t Reg1, uint32_t Reg2);

/************************** Branch functions  ************************/
void CArmRecompilerOps::Compile_BranchCompare(BRANCH_COMPARE CompareType)
{
    switch (CompareType)
    {
    case CompareTypeBEQ: BEQ_Compare(); break;
    case CompareTypeBNE: BNE_Compare(); break;
    case CompareTypeBLTZ: BLTZ_Compare(); break;
    case CompareTypeBLEZ: BLEZ_Compare(); break;
    case CompareTypeBGTZ: BGTZ_Compare(); break;
    case CompareTypeBGEZ: BGEZ_Compare(); break;
    case CompareTypeCOP1BCF: COP1_BCF_Compare(); break;
    case CompareTypeCOP1BCT: COP1_BCT_Compare(); break;
    default:
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CArmRecompilerOps::Compile_Branch(BRANCH_COMPARE CompareType, BRANCH_TYPE BranchType, bool Link)
{
    static CRegInfo RegBeforeDelay;
    static bool EffectDelaySlot;
    OPCODE Command = {0};

    if (m_NextInstruction == NORMAL)
    {
        if (CompareType == CompareTypeCOP1BCF || CompareType == CompareTypeCOP1BCT)
        {
            CompileCop1Test();
        }
        if (m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4 == m_CompilePC + 8)
        {
            return;
        }

        if ((m_CompilePC & 0xFFC) != 0xFFC)
        {
            switch (BranchType)
            {
            case BranchTypeRs: EffectDelaySlot = DelaySlotEffectsCompare(m_CompilePC, m_Opcode.rs, 0); break;
            case BranchTypeRsRt: EffectDelaySlot = DelaySlotEffectsCompare(m_CompilePC, m_Opcode.rs, m_Opcode.rt); break;
            case BranchTypeCop1:

                if (!g_MMU->LW_VAddr(m_CompilePC + 4, Command.Hex))
                {
                    g_Notify->FatalError(GS(MSG_FAIL_LOAD_WORD));
                }

                EffectDelaySlot = false;
                if (Command.op == R4300i_CP1)
                {
                    if ((Command.fmt == R4300i_COP1_S && (Command.funct & 0x30) == 0x30) ||
                        (Command.fmt == R4300i_COP1_D && (Command.funct & 0x30) == 0x30))
                    {
                        EffectDelaySlot = true;
                    }
                }
                break;
            default:
                if (bHaveDebugger()) { g_Notify->DisplayError("Unknown branch type"); }
            }
        }
        else
        {
            EffectDelaySlot = true;
        }
        m_Section->m_Jump.JumpPC = m_CompilePC;
        m_Section->m_Jump.TargetPC = m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4;
        if (m_Section->m_JumpSection != NULL)
        {
            m_Section->m_Jump.BranchLabel.Format("Section_%d", m_Section->m_JumpSection->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel.Format("Exit_%X_jump_%X", m_Section->m_EnterPC, m_Section->m_Jump.TargetPC);
        }
        m_Section->m_Jump.LinkLocation = NULL;
        m_Section->m_Jump.LinkLocation2 = NULL;
        m_Section->m_Jump.DoneDelaySlot = false;
        m_Section->m_Cont.JumpPC = m_CompilePC;
        m_Section->m_Cont.TargetPC = m_CompilePC + 8;
        if (m_Section->m_ContinueSection != NULL)
        {
            m_Section->m_Cont.BranchLabel.Format("Section_%d", m_Section->m_ContinueSection->m_SectionID);
        }
        else
        {
            m_Section->m_Cont.BranchLabel.Format("Exit_%X_continue_%X", m_Section->m_EnterPC, m_Section->m_Cont.TargetPC);
        }
        m_Section->m_Cont.LinkLocation = NULL;
        m_Section->m_Cont.LinkLocation2 = NULL;
        m_Section->m_Cont.DoneDelaySlot = false;
        if (m_Section->m_Jump.TargetPC < m_Section->m_Cont.TargetPC)
        {
            m_Section->m_Cont.FallThrough = false;
            m_Section->m_Jump.FallThrough = true;
        }
        else
        {
            m_Section->m_Cont.FallThrough = true;
            m_Section->m_Jump.FallThrough = false;
        }

        if (Link)
        {
            UnMap_GPR(31, false);
            if (!g_System->b32BitCore())
            {
                MoveConstToVariable((m_CompilePC & 0x80000000) != 0 ? 0xFFFFFFFF : 0, &_GPR[31].UW[1], CRegName::GPR_Hi[31]);
            }
            MoveConstToVariable(m_CompilePC + 8, &_GPR[31].UW[0], CRegName::GPR_Lo[31]);
        }
        if (EffectDelaySlot)
        {
            if ((m_CompilePC & 0xFFC) != 0xFFC)
            {
                m_Section->m_Cont.BranchLabel = m_Section->m_ContinueSection != NULL ? "Continue" : "ContinueExitBlock";
                m_Section->m_Jump.BranchLabel = m_Section->m_JumpSection != NULL ? "Jump" : "JumpExitBlock";
            }
            else
            {
                m_Section->m_Cont.BranchLabel = "Continue";
                m_Section->m_Jump.BranchLabel = "Jump";
            }
            if (m_Section->m_Jump.TargetPC != m_Section->m_Cont.TargetPC)
            {
                Compile_BranchCompare(CompareType);
            }
            if (!m_Section->m_Jump.FallThrough && !m_Section->m_Cont.FallThrough)
            {
                if (m_Section->m_Jump.LinkLocation != NULL)
                {
                    CPU_Message("");
                    CPU_Message("      %s:", m_Section->m_Jump.BranchLabel.c_str());
                    LinkJump(m_Section->m_Jump);
                    m_Section->m_Jump.FallThrough = true;
                }
                else if (m_Section->m_Cont.LinkLocation != NULL)
                {
                    CPU_Message("");
                    CPU_Message("      %s:", m_Section->m_Cont.BranchLabel.c_str());
                    LinkJump(m_Section->m_Cont);
                    m_Section->m_Cont.FallThrough = true;
                }
            }
            if ((m_CompilePC & 0xFFC) == 0xFFC)
            {
                uint8_t * DelayLinkLocation = NULL;
                if (m_Section->m_Jump.FallThrough)
                {
                    if (m_Section->m_Jump.LinkLocation != NULL || m_Section->m_Jump.LinkLocation2 != NULL)
                    {
                        g_Notify->BreakPoint(__FILE__, __LINE__);
                    }
                    MoveConstToVariable(m_Section->m_Jump.TargetPC, &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
                }
                else if (m_Section->m_Cont.FallThrough)
                {
                    if (m_Section->m_Cont.LinkLocation != NULL || m_Section->m_Cont.LinkLocation2 != NULL)
                    {
                        g_Notify->BreakPoint(__FILE__, __LINE__);
                    }
                    MoveConstToVariable(m_Section->m_Cont.TargetPC, &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
                }

                if (m_Section->m_Jump.LinkLocation != NULL || m_Section->m_Jump.LinkLocation2 != NULL)
                {
                    if (DelayLinkLocation != NULL) { g_Notify->BreakPoint(__FILE__, __LINE__); }
                    DelayLinkLocation = *g_RecompPos;
                    BranchLabel8(ArmBranch_Always,"DoDelaySlot");

                    CPU_Message("      ");
                    CPU_Message("      %s:", m_Section->m_Jump.BranchLabel.c_str());
                    LinkJump(m_Section->m_Jump);
                    MoveConstToVariable(m_Section->m_Jump.TargetPC, &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
                }
                if (m_Section->m_Cont.LinkLocation != NULL || m_Section->m_Cont.LinkLocation2 != NULL)
                {
                    if (DelayLinkLocation != NULL) { g_Notify->BreakPoint(__FILE__, __LINE__); }
                    DelayLinkLocation = *g_RecompPos;
                    BranchLabel8(ArmBranch_Always,"DoDelaySlot");

                    CPU_Message("      ");
                    CPU_Message("      %s:", m_Section->m_Cont.BranchLabel.c_str());
                    LinkJump(m_Section->m_Cont);
                    MoveConstToVariable(m_Section->m_Cont.TargetPC, &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
                }
                if (DelayLinkLocation)
                {
                    CPU_Message("");
                    CPU_Message("      DoDelaySlot:");
                    SetJump8(DelayLinkLocation, *g_RecompPos);
                }
                OverflowDelaySlot(false);
                return;
            }
            ResetRegProtection();
            RegBeforeDelay = m_RegWorkingSet;
        }
        m_NextInstruction = DO_DELAY_SLOT;
    }
    else if (m_NextInstruction == DELAY_SLOT_DONE)
    {
        if (EffectDelaySlot)
        {
            CJumpInfo * FallInfo = m_Section->m_Jump.FallThrough ? &m_Section->m_Jump : &m_Section->m_Cont;
            CJumpInfo * JumpInfo = m_Section->m_Jump.FallThrough ? &m_Section->m_Cont : &m_Section->m_Jump;

            if (FallInfo->FallThrough && !FallInfo->DoneDelaySlot)
            {
                ResetRegProtection();
                FallInfo->RegSet = m_RegWorkingSet;
                if (FallInfo == &m_Section->m_Jump)
                {
                    if (m_Section->m_JumpSection != NULL)
                    {
                        m_Section->m_Jump.BranchLabel.Format("Section_%d", m_Section->m_JumpSection->m_SectionID);
                    }
                    else
                    {
                        m_Section->m_Jump.BranchLabel = "ExitBlock";
                    }
                    if (FallInfo->TargetPC <= m_CompilePC)
                    {
                        UpdateCounters(m_Section->m_Jump.RegSet, true, true);
                        CPU_Message("CompileSystemCheck 12");
                        CompileSystemCheck(FallInfo->TargetPC, m_Section->m_Jump.RegSet);
                        ResetRegProtection();
                        FallInfo->ExitReason = CExitInfo::Normal_NoSysCheck;
                        FallInfo->JumpPC = (uint32_t)-1;
                    }
                }
                else
                {
                    if (m_Section->m_ContinueSection != NULL)
                    {
                        m_Section->m_Cont.BranchLabel.Format("Section_%d", m_Section->m_ContinueSection->m_SectionID);
                    }
                    else
                    {
                        m_Section->m_Cont.BranchLabel = "ExitBlock";
                    }
                }
                FallInfo->DoneDelaySlot = true;
                if (!JumpInfo->DoneDelaySlot)
                {
                    FallInfo->FallThrough = false;
                    BranchLabel20(ArmBranch_Always, FallInfo->BranchLabel.c_str());
                    FallInfo->LinkLocation = (uint32_t *)(*g_RecompPos - 4);

                    if (JumpInfo->LinkLocation != NULL)
                    {
                        CPU_Message("      %s:", JumpInfo->BranchLabel.c_str());
                        LinkJump(*JumpInfo);
                        JumpInfo->FallThrough = true;
                        m_NextInstruction = DO_DELAY_SLOT;
                        m_RegWorkingSet = RegBeforeDelay;
                        return;
                    }
                }
            }
        }
        else
        {
            if (m_Section->m_Jump.TargetPC != m_Section->m_Cont.TargetPC)
            {
                Compile_BranchCompare(CompareType);
                ResetRegProtection();
                m_Section->m_Cont.RegSet = m_RegWorkingSet;
                m_Section->m_Jump.RegSet = m_RegWorkingSet;
            }
            else
            {
                m_Section->m_Jump.FallThrough = false;
                m_Section->m_Cont.FallThrough = true;
                m_Section->m_Cont.RegSet = m_RegWorkingSet;
                if (m_Section->m_ContinueSection == NULL && m_Section->m_JumpSection != NULL)
                {
                    m_Section->m_ContinueSection = m_Section->m_JumpSection;
                    m_Section->m_JumpSection = NULL;
                }
                if (m_Section->m_ContinueSection != NULL)
                {
                    m_Section->m_Cont.BranchLabel.Format("Section_%d", m_Section->m_ContinueSection->m_SectionID);
                }
                else
                {
                    m_Section->m_Cont.BranchLabel = "ExitBlock";
                }
            }
        }
        m_Section->GenerateSectionLinkage();
        m_NextInstruction = END_BLOCK;
    }
    else
    {
        if (bHaveDebugger())
        {
            g_Notify->DisplayError(stdstr_f("WTF\n%s\nNextInstruction = %X", __FUNCTION__, m_NextInstruction).c_str());
        }
    }
}

void CArmRecompilerOps::Compile_BranchLikely(BRANCH_COMPARE CompareType, bool Link)
{
    if (m_NextInstruction == NORMAL)
    {
        if (CompareType == CompareTypeCOP1BCF || CompareType == CompareTypeCOP1BCT)
        {
            CompileCop1Test();
        }
        if (!g_System->bLinkBlocks() || (m_CompilePC & 0xFFC) == 0xFFC)
        {
            m_Section->m_Jump.JumpPC = m_CompilePC;
            m_Section->m_Jump.TargetPC = m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4;
            m_Section->m_Cont.JumpPC = m_CompilePC;
            m_Section->m_Cont.TargetPC = m_CompilePC + 8;
        }
        else
        {
            if (m_Section->m_Jump.JumpPC != m_CompilePC)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
            if (m_Section->m_Cont.JumpPC != m_CompilePC)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
            if (m_Section->m_Cont.TargetPC != m_CompilePC + 8)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
        }

        if (m_Section->m_JumpSection != NULL)
        {
            m_Section->m_Jump.BranchLabel.Format("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = "ExitBlock";
        }

        if (m_Section->m_ContinueSection != NULL)
        {
            m_Section->m_Cont.BranchLabel.Format("Section_%d", ((CCodeSection *)m_Section->m_ContinueSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Cont.BranchLabel = "ExitBlock";
        }

        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Jump.LinkLocation = NULL;
        m_Section->m_Jump.LinkLocation2 = NULL;
        m_Section->m_Cont.FallThrough = false;
        m_Section->m_Cont.LinkLocation = NULL;
        m_Section->m_Cont.LinkLocation2 = NULL;

        if (Link)
        {
            UnMap_GPR(31, false);
            g_Notify->BreakPoint(__FILE__, __LINE__);
#ifdef tofix
            m_RegWorkingSet.SetMipsRegLo(31, m_CompilePC + 8);
            m_RegWorkingSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
#endif
        }

        Compile_BranchCompare(CompareType);
        ResetRegProtection();

        m_Section->m_Cont.RegSet = m_RegWorkingSet;
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            if (m_Section->m_Cont.FallThrough)
            {
                if (m_Section->m_Jump.LinkLocation != NULL)
                {
                    g_Notify->BreakPoint(__FILE__, __LINE__);
                }
            }

            if (m_Section->m_Jump.LinkLocation != NULL || m_Section->m_Jump.FallThrough)
            {
                LinkJump(m_Section->m_Jump);

                MoveConstToVariable(m_Section->m_Jump.TargetPC, &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
                OverflowDelaySlot(false);
                CPU_Message("      ");
                CPU_Message("      %s:", m_Section->m_Cont.BranchLabel.c_str());
            }
            else if (!m_Section->m_Cont.FallThrough)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }

            LinkJump(m_Section->m_Cont);
            CompileExit(m_CompilePC, m_CompilePC + 8, m_Section->m_Cont.RegSet, CExitInfo::Normal);
            return;
        }
        else
        {
            m_NextInstruction = DO_DELAY_SLOT;
        }

        if (g_System->bLinkBlocks())
        {
            m_Section->m_Jump.RegSet = m_RegWorkingSet;
            m_Section->GenerateSectionLinkage();
            m_NextInstruction = END_BLOCK;
        }
        else
        {
            if (m_Section->m_Cont.FallThrough)
            {
                if (m_Section->m_Jump.LinkLocation != NULL)
                {
                    g_Notify->BreakPoint(__FILE__, __LINE__);
                }
                m_Section->GenerateSectionLinkage();
                m_NextInstruction = END_BLOCK;
            }
        }
    }
    else if (m_NextInstruction == DELAY_SLOT_DONE)
    {
        ResetRegProtection();
        m_Section->m_Jump.RegSet = m_RegWorkingSet;
        m_Section->GenerateSectionLinkage();
        m_NextInstruction = END_BLOCK;
    }
    else if (bHaveDebugger())
    {
        g_Notify->DisplayError(stdstr_f("WTF\n%s\nNextInstruction = %X", __FUNCTION__, m_NextInstruction).c_str());
    }
}

void CArmRecompilerOps::BNE_Compare()
{
    if (IsKnown(m_Opcode.rs) || IsKnown(m_Opcode.rt))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
        CArmRecompilerOps::UnknownOpcode();
    }
    else
    {
        uint8_t * Jump = NULL;

        //r0 = low, r1 = high
        //r2 = low, r3 = high
        if (!g_System->b32BitCore())
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[1],CRegName::GPR_Hi[m_Opcode.rs], Arm_R1);
            MoveVariableToArmReg(&_GPR[m_Opcode.rt].UW[1],CRegName::GPR_Hi[m_Opcode.rt], Arm_R3);
            CompareArmRegToArmReg(Arm_R1,Arm_R3);

            if (m_Section->m_Jump.FallThrough)
            {
                Jump = *g_RecompPos;
                BranchLabel8(ArmBranch_Notequal, "continue");
            }
            else
            {
                BranchLabel20(ArmBranch_Notequal, m_Section->m_Jump.BranchLabel.c_str());
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
        }

        MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0],CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
        MoveVariableToArmReg(&_GPR[m_Opcode.rt].UW[0],CRegName::GPR_Lo[m_Opcode.rt], Arm_R2);
        CompareArmRegToArmReg(Arm_R0,Arm_R2);
        if (m_Section->m_Cont.FallThrough)
        {
            BranchLabel20(ArmBranch_Notequal, m_Section->m_Jump.BranchLabel.c_str());
            if (g_System->b32BitCore())
            {
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else
            {
                m_Section->m_Jump.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
            }
        }
        else if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_Equal, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            if (Jump)
            {
                CPU_Message("      ");
                CPU_Message("      continue:");
                SetJump8(Jump, *g_RecompPos);
            }
        }
        else
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
#ifdef tofix
            JeLabel32(m_Section->m_Cont.BranchLabel.c_str(), 0);
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            JmpLabel32(m_Section->m_Jump.BranchLabel.c_str(), 0);
            if (g_System->b32BitCore())
            {
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else
            {
                m_Section->m_Jump.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
            }
#endif
        }
    }
}

void CArmRecompilerOps::BEQ_Compare()
{
    if (IsKnown(m_Opcode.rs) || IsKnown(m_Opcode.rt))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
        CArmRecompilerOps::UnknownOpcode();
    }
    else
    {
        uint8_t * Jump = NULL;

        //r0 = low, r1 = high
        //r2 = low, r3 = high
        if (!g_System->b32BitCore())
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[1],CRegName::GPR_Hi[m_Opcode.rs], Arm_R1);
            MoveVariableToArmReg(&_GPR[m_Opcode.rt].UW[1],CRegName::GPR_Hi[m_Opcode.rt], Arm_R3);
            CompareArmRegToArmReg(Arm_R1,Arm_R3);

            if (m_Section->m_Cont.FallThrough)
            {
                Jump = *g_RecompPos;
                BranchLabel8(ArmBranch_Notequal, "continue");
            }
            else
            {
                BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
        }
        MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
        MoveVariableToArmReg(&_GPR[m_Opcode.rt].UW[0], CRegName::GPR_Lo[m_Opcode.rt], Arm_R2);
        CompareArmRegToArmReg(Arm_R0, Arm_R2);
        if (m_Section->m_Cont.FallThrough)
        {
            BranchLabel20(ArmBranch_Equal, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);

            if (Jump)
            {
                CPU_Message("      ");
                CPU_Message("      continue:");
                SetJump8(Jump, *g_RecompPos);
            }
        }
        else if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
            if (g_System->b32BitCore())
            {
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else
            {
                m_Section->m_Cont.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
            }
        }
        else
        {
            BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
            if (g_System->b32BitCore())
            {
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else
            {
                m_Section->m_Cont.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
            }
            BranchLabel20(ArmBranch_Always, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
    }
}

void CArmRecompilerOps::BGTZ_Compare()
{
    if (IsKnown(m_Opcode.rs) || IsKnown(m_Opcode.rt))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
        CArmRecompilerOps::UnknownOpcode();
    }
    else if (g_System->b32BitCore())
    {
        //r0 = low, r1 = high
        //r2 = low, r3 = high
        MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
        MoveConstToArmReg((uint32_t)0, Arm_R2);
        CompareArmRegToArmReg(Arm_R0, Arm_R2);
        if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_LessThanOrEqual, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else if (m_Section->m_Cont.FallThrough)
        {
            BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else
        {
            BranchLabel20(ArmBranch_LessThanOrEqual, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            BranchLabel20(ArmBranch_Always, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
    }
    else
    {
        uint8_t *Jump = NULL;

        MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[1], CRegName::GPR_Hi[m_Opcode.rs], Arm_R0);
        MoveConstToArmReg((uint32_t)0, Arm_R2);
        CompareArmRegToArmReg(Arm_R0, Arm_R2);
        if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            Jump = *g_RecompPos;
            BranchLabel8(ArmBranch_GreaterThan, "continue");
        }
        else if (m_Section->m_Cont.FallThrough)
        {
            Jump = *g_RecompPos;
            BranchLabel8(ArmBranch_LessThan, "continue");
            BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else
        {
            BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }

        MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
        CompareArmRegToArmReg(Arm_R0, Arm_R2);
        if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_Equal, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
            CPU_Message("      continue:");
            SetJump8(Jump, *g_RecompPos);
        }
        else if (m_Section->m_Cont.FallThrough)
        {
            BranchLabel20(ArmBranch_Notequal, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            CPU_Message("      continue:");
            SetJump8(Jump, *g_RecompPos);
        }
        else
        {
            BranchLabel20(ArmBranch_Notequal, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            BranchLabel20(ArmBranch_Always, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
        }
    }
}

void CArmRecompilerOps::BLEZ_Compare()
{
    if (IsKnown(m_Opcode.rs) || IsKnown(m_Opcode.rt))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
        CArmRecompilerOps::UnknownOpcode();
    }
    else
    {
        uint8_t *Jump = NULL;

        if (!g_System->b32BitCore())
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[1], CRegName::GPR_Hi[m_Opcode.rs], Arm_R0);
            MoveConstToArmReg((uint32_t)0, Arm_R2);
            CompareArmRegToArmReg(Arm_R0, Arm_R2);
            if (m_Section->m_Jump.FallThrough)
            {
                BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
                Jump = *g_RecompPos;
                BranchLabel8(ArmBranch_LessThan, "Continue");
            }
            else if (m_Section->m_Cont.FallThrough)
            {
                Jump = *g_RecompPos;
                BranchLabel8(ArmBranch_GreaterThan, "Continue");
                BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else
            {
                BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
                BranchLabel20(ArmBranch_LessThan, m_Section->m_Jump.BranchLabel.c_str());
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
            CompareArmRegToArmReg(Arm_R0, Arm_R2);
            if (m_Section->m_Jump.FallThrough)
            {
                BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
                if (Jump)
                {
                    CPU_Message("      continue:");
                    SetJump8(Jump, *g_RecompPos);
                }
            }
            else if (m_Section->m_Cont.FallThrough)
            {
                BranchLabel20(ArmBranch_Equal, m_Section->m_Jump.BranchLabel.c_str());
                m_Section->m_Jump.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
                if (Jump)
                {
                    CPU_Message("      continue:");
                    SetJump8(Jump, *g_RecompPos);
                }
            }
            else
            {
                BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
                BranchLabel20(ArmBranch_Always, "BranchToJump");
                m_Section->m_Jump.LinkLocation2 = (uint32_t *)(*g_RecompPos - 4);
            }
        }
        else
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
            MoveConstToArmReg((uint32_t)0, Arm_R2);
            CompareArmRegToArmReg(Arm_R0, Arm_R2);
            if (m_Section->m_Jump.FallThrough)
            {
                BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else if (m_Section->m_Cont.FallThrough)
            {
                BranchLabel20(ArmBranch_LessThanOrEqual, m_Section->m_Jump.BranchLabel.c_str());
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
            else
            {
                BranchLabel20(ArmBranch_GreaterThan, m_Section->m_Cont.BranchLabel.c_str());
                m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
                BranchLabel20(ArmBranch_Always, m_Section->m_Jump.BranchLabel.c_str());
                m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            }
        }
    }
}

void CArmRecompilerOps::BLTZ_Compare()
{
    if (IsKnown(m_Opcode.rs))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
    else
    {
        if (g_System->b32BitCore())
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
        }
        else
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[1], CRegName::GPR_Hi[m_Opcode.rs], Arm_R0);
        }
        MoveConstToArmReg((uint32_t)0, Arm_R2);
        CompareArmRegToArmReg(Arm_R0, Arm_R2);
        if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_GreaterThanOrEqual, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else if (m_Section->m_Cont.FallThrough)
        {
            BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else
        {
            BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            BranchLabel20(ArmBranch_Always, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
    }
}

void CArmRecompilerOps::BGEZ_Compare()
{
    if (m_Opcode.rs == 0)
    {
        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Cont.FallThrough = false;
    }
    else if (IsKnown(m_Opcode.rs))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
    else
    {
        if (g_System->b32BitCore())
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
        }
        else
        {
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[1], CRegName::GPR_Hi[m_Opcode.rs], Arm_R0);
        }
        MoveConstToArmReg((uint32_t)0, Arm_R2);
        CompareArmRegToArmReg(Arm_R0, Arm_R2);
        if (m_Section->m_Cont.FallThrough)
        {
            BranchLabel20(ArmBranch_GreaterThanOrEqual, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else if (m_Section->m_Jump.FallThrough)
        {
            BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
        else
        {
            BranchLabel20(ArmBranch_LessThan, m_Section->m_Cont.BranchLabel.c_str());
            m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
            BranchLabel20(ArmBranch_Always, m_Section->m_Jump.BranchLabel.c_str());
            m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        }
    }
}

void CArmRecompilerOps::COP1_BCF_Compare()
{
    TestVariable(FPCSR_C, &_FPCR[31], "_FPCR[31]");
    if (m_Section->m_Cont.FallThrough)
    {
        BranchLabel20(ArmBranch_Notequal, m_Section->m_Jump.BranchLabel.c_str());
        m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
    }
    else if (m_Section->m_Jump.FallThrough)
    {
        BranchLabel20(ArmBranch_Equal, m_Section->m_Cont.BranchLabel.c_str());
        m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
    }
    else
    {
        BranchLabel20(ArmBranch_Equal, m_Section->m_Cont.BranchLabel.c_str());
        m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        BranchLabel20(ArmBranch_Always, m_Section->m_Jump.BranchLabel.c_str());
        m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
    }
}

void CArmRecompilerOps::COP1_BCT_Compare()
{
    TestVariable(FPCSR_C, &_FPCR[31], "_FPCR[31]");
    if (m_Section->m_Cont.FallThrough)
    {
        BranchLabel20(ArmBranch_Equal, m_Section->m_Jump.BranchLabel.c_str());
        m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
    }
    else if (m_Section->m_Jump.FallThrough)
    {
        BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
        m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
    }
    else
    {
        BranchLabel20(ArmBranch_Notequal, m_Section->m_Cont.BranchLabel.c_str());
        m_Section->m_Cont.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
        BranchLabel20(ArmBranch_Always, m_Section->m_Jump.BranchLabel.c_str());
        m_Section->m_Jump.LinkLocation = (uint32_t *)(*g_RecompPos - 4);
    }
}

/*************************  OpCode functions *************************/
void CArmRecompilerOps::J()
{
    if (m_NextInstruction == NORMAL)
    {
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            MoveConstToVariable((m_CompilePC & 0xF0000000) + (m_Opcode.target << 2), &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
            OverflowDelaySlot(false);
            return;
        }

        m_Section->m_Jump.TargetPC = (m_CompilePC & 0xF0000000) + (m_Opcode.target << 2);;
        m_Section->m_Jump.JumpPC = m_CompilePC;
        if (m_Section->m_JumpSection != NULL)
        {
            m_Section->m_Jump.BranchLabel.Format("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = "ExitBlock";
        }
        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Jump.LinkLocation = NULL;
        m_Section->m_Jump.LinkLocation2 = NULL;
        m_NextInstruction = DO_DELAY_SLOT;
    }
    else if (m_NextInstruction == DELAY_SLOT_DONE)
    {
        m_Section->m_Jump.RegSet = m_RegWorkingSet;
        m_Section->GenerateSectionLinkage();
        m_NextInstruction = END_BLOCK;
    }
    else if (bHaveDebugger())
    {
        g_Notify->DisplayError(stdstr_f("WTF\n%s\nNextInstruction = %X", __FUNCTION__, m_NextInstruction).c_str());
    }
}

void CArmRecompilerOps::JAL()
{
    if (m_NextInstruction == NORMAL)
    {
        if (IsKnown(31))
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
            return;
        }

        if (!g_System->b32BitCore())
        {
            MoveConstToVariable((m_CompilePC & 0x80000000) != 0 ? 0xFFFFFFFF : 0, &_GPR[31].UW[1], CRegName::GPR_Hi[31]);
        }
        MoveConstToVariable(m_CompilePC + 8, &_GPR[31].UW[0], CRegName::GPR_Lo[31]);
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            MoveConstToVariable((m_CompilePC & 0xF0000000) + (m_Opcode.target << 2), &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
            OverflowDelaySlot(false);
            return;
        }
        m_Section->m_Jump.TargetPC = (m_CompilePC & 0xF0000000) + (m_Opcode.target << 2);
        m_Section->m_Jump.JumpPC = m_CompilePC;
        if (m_Section->m_JumpSection != NULL)
        {
            m_Section->m_Jump.BranchLabel.Format("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = "ExitBlock";
        }
        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Jump.LinkLocation = NULL;
        m_Section->m_Jump.LinkLocation2 = NULL;
        m_NextInstruction = DO_DELAY_SLOT;
    }
    else if (m_NextInstruction == DELAY_SLOT_DONE)
    {
        if (m_Section->m_JumpSection)
        {
            m_Section->m_Jump.RegSet = m_RegWorkingSet;
            m_Section->GenerateSectionLinkage();
        }
        else
        {
            m_RegWorkingSet.WriteBackRegisters();

            MoveConstToArmReg((uint32_t)_PROGRAM_COUNTER, Arm_R0, "_PROGRAM_COUNTER");
            LoadArmRegPointerToArmReg(Arm_R0, Arm_R1, 0);
            MoveConstToArmReg(0xF0000000, Arm_R2);
            MoveConstToArmReg((uint32_t)(m_Opcode.target << 2), Arm_R3);
            AndArmRegToArmReg(Arm_R2, Arm_R1);
            AddArmRegToArmReg(Arm_R3, Arm_R1, Arm_R1);
            StoreArmRegToArmRegPointer(Arm_R1, Arm_R0, 0);

            uint32_t TargetPC = (m_CompilePC & 0xF0000000) + (m_Opcode.target << 2);
            bool bCheck = TargetPC <= m_CompilePC;
            UpdateCounters(m_RegWorkingSet, bCheck, true);

            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, bCheck ? CExitInfo::Normal : CExitInfo::Normal_NoSysCheck);
        }
        m_NextInstruction = END_BLOCK;
    }
    else
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CArmRecompilerOps::ADDI()
{
    if (m_Opcode.rt == 0 || (m_Opcode.immediate == 0 && m_Opcode.rs == m_Opcode.rt))
    {
        return;
    }

    if (g_System->bFastSP())
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }

    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::ADDI, "R4300iOp32::ADDI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::ADDI, "R4300iOp::ADDI");
    }
}

void CArmRecompilerOps::ADDIU()
{
    if (m_Opcode.rt == 0 || (m_Opcode.immediate == 0 && m_Opcode.rs == m_Opcode.rt))
    {
        return;
    }

    if (g_System->bFastSP())
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }

    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::ADDIU, "R4300iOp32::ADDIU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::ADDIU, "R4300iOp::ADDIU");
    }
}

void CArmRecompilerOps::SLTI()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SLTI, "R4300iOp32::SLTI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SLTI, "R4300iOp::SLTI");
    }
}

void CArmRecompilerOps::SLTIU()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SLTIU, "R4300iOp32::SLTIU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SLTIU, "R4300iOp::SLTIU");
    }
}

void CArmRecompilerOps::ANDI()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::ANDI, "R4300iOp32::ANDI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::ANDI, "R4300iOp::ANDI");
    }
}

void CArmRecompilerOps::ORI()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::ORI, "R4300iOp32::ORI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::ORI, "R4300iOp::ORI");
    }
}

void CArmRecompilerOps::XORI()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::XORI, "R4300iOp32::XORI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::XORI, "R4300iOp::XORI");
    }
}

void CArmRecompilerOps::LUI()
{
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LUI, "R4300iOp32::LUI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LUI, "R4300iOp::LUI");
    }
}

void CArmRecompilerOps::DADDIU()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::DADDIU, "R4300iOp32::DADDIU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::DADDIU, "R4300iOp::DADDIU");
    }
}

void CArmRecompilerOps::LDL()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LDL, "R4300iOp32::LDL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LDL, "R4300iOp::LDL");
    }
}

void CArmRecompilerOps::LDR()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LDR, "R4300iOp32::LDR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LDR, "R4300iOp::LDR");
    }
}

void CArmRecompilerOps::LB()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LB, "R4300iOp32::LB");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LB, "R4300iOp::LB");
    }
}

void CArmRecompilerOps::LH()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LH, "R4300iOp32::LH");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LH, "R4300iOp::LH");
    }
}

void CArmRecompilerOps::LWL()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LWL, "R4300iOp32::LWL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LWL, "R4300iOp::LWL");
    }
}

void CArmRecompilerOps::LW()
{
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() - g_System->CountPerOp());
    UpdateCounters(m_RegWorkingSet, false, true);
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_System->CountPerOp());

    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LW, "R4300iOp32::LW");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LW, "R4300iOp::LW");
    }
}

void CArmRecompilerOps::LBU()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LBU, "R4300iOp32::LBU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LBU, "R4300iOp::LBU");
    }
}

void CArmRecompilerOps::LHU()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LHU, "R4300iOp32::LHU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LHU, "R4300iOp::LHU");
    }
}

void CArmRecompilerOps::LWR()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LWR, "R4300iOp32::LWR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LWR, "R4300iOp::LWR");
    }
}

void CArmRecompilerOps::LWU()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LWU, "R4300iOp32::LWU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LWU, "R4300iOp::LWU");
    }
}

void CArmRecompilerOps::SB()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SB, "R4300iOp32::SB");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SB, "R4300iOp::SB");
    }
}

void CArmRecompilerOps::SH()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SH, "R4300iOp32::SH");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SH, "R4300iOp::SH");
    }
}

void CArmRecompilerOps::SWL()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SWL, "R4300iOp32::SWL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SWL, "R4300iOp::SWL");
    }
}

void CArmRecompilerOps::SW()
{
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() - g_System->CountPerOp());
    UpdateCounters(m_RegWorkingSet, false, true);
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_System->CountPerOp());

    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SW, "R4300iOp32::SW");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SW, "R4300iOp::SW");
    }
}

void CArmRecompilerOps::SWR()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SWR, "R4300iOp32::SWR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SWR, "R4300iOp::SWR");
    }
}

void CArmRecompilerOps::SDL()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SDL, "R4300iOp32::SDL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SDL, "R4300iOp::SDL");
    }
}

void CArmRecompilerOps::SDR()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SDR, "R4300iOp32::SDR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SDR, "R4300iOp::SDR");
    }
}

void CArmRecompilerOps::CACHE()
{
    if (g_Settings->LoadDword(Game_SMM_Cache) == 0)
    {
        return;
    }

    UnMap_GPR(m_Opcode.base, true);

    switch (m_Opcode.rt)
    {
    case 0:
    case 16:
        m_RegWorkingSet.BeforeCallDirect();
        MoveConstToArmReg((uint32_t)CRecompiler::Remove_Cache, Arm_R3, "CRecompiler::Remove_Cache");
        MoveConstToArmReg((uint32_t)0x20, Arm_R2);
        MoveVariableToArmReg(&_GPR[m_Opcode.base].UW[0], CRegName::GPR_Lo[m_Opcode.base], Arm_R1);
        MoveConstToArmReg((uint32_t)((int16_t)m_Opcode.offset), Arm_R0);
        AddArmRegToArmReg(Arm_R0, Arm_R1, Arm_R1);
        MoveConstToArmReg((uint32_t)g_Recompiler, Arm_R0, "g_Recompiler");
        CallFunction((void *)AddressOf(&CRecompiler::ClearRecompCode_Virt), "CRecompiler::ClearRecompCode_Virt");
        m_RegWorkingSet.AfterCallDirect();
        break;
    case 1:
    case 3:
    case 13:
    case 5:
    case 8:
    case 9:
    case 17:
    case 21:
    case 25:
        break;
    default:
        if (bHaveDebugger())
        {
            g_Notify->DisplayError(stdstr_f("cache: %d", m_Opcode.rt).c_str());
        }
    }
}

void CArmRecompilerOps::LL()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LL, "R4300iOp32::LL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LL, "R4300iOp::LL");
    }
}

void CArmRecompilerOps::LWC1()
{
    CompileCop1Test();
    UnMap_GPR(m_Opcode.base, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LWC1, "R4300iOp32::LWC1");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LWC1, "R4300iOp::LWC1");
    }
}

void CArmRecompilerOps::LDC1()
{
    CompileCop1Test();
    UnMap_GPR(m_Opcode.base, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LDC1, "R4300iOp32::LDC1");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LDC1, "R4300iOp::LDC1");
    }
}

void CArmRecompilerOps::LD()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::LD, "R4300iOp32::LD");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::LD, "R4300iOp::LD");
    }
}

void CArmRecompilerOps::SC()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SC, "R4300iOp32::SC");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SC, "R4300iOp::SC");
    }
}

void CArmRecompilerOps::SWC1()
{
    CompileCop1Test();
    UnMap_GPR(m_Opcode.base, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SWC1, "R4300iOp32::SWC1");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SWC1, "R4300iOp::SWC1");
    }
}

void CArmRecompilerOps::SDC1()
{
    CompileCop1Test();
    UnMap_GPR(m_Opcode.base, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SDC1, "R4300iOp32::SDC1");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SDC1, "R4300iOp::SDC1");
    }
}

void CArmRecompilerOps::SD()
{
    UnMap_GPR(m_Opcode.base, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SD, "R4300iOp32::SD");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SD, "R4300iOp::SD");
    }
}

void CArmRecompilerOps::SPECIAL_SLL()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SLL, "R4300iOp32::SPECIAL_SLL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SLL, "R4300iOp::SPECIAL_SLL");
    }
}

void CArmRecompilerOps::SPECIAL_SRL()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SRL, "R4300iOp32::SPECIAL_SRL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SRL, "R4300iOp::SPECIAL_SRL");
    }
}

void CArmRecompilerOps::SPECIAL_SRA()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SRA, "R4300iOp32::SPECIAL_SRA");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SRA, "R4300iOp::SPECIAL_SRA");
    }
}

void CArmRecompilerOps::SPECIAL_SLLV()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SLLV, "R4300iOp32::SPECIAL_SLLV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SLLV, "R4300iOp::SPECIAL_SLLV");
    }
}

void CArmRecompilerOps::SPECIAL_SRLV()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SRLV, "R4300iOp32::SPECIAL_SRLV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SRLV, "R4300iOp::SPECIAL_SRLV");
    }
}

void CArmRecompilerOps::SPECIAL_SRAV()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SRAV, "R4300iOp32::SPECIAL_SRAV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SRAV, "R4300iOp::SPECIAL_SRAV");
    }
}

void CArmRecompilerOps::SPECIAL_JR()
{
    if (m_NextInstruction == NORMAL)
    {
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            if (IsKnown(m_Opcode.rs))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
#ifdef tofix
                MoveX86regToVariable(GetMipsRegMapLo(m_Opcode.rs), &R4300iOp::m_JumpToLocation, "R4300iOp::m_JumpToLocation");
                m_RegWorkingSet.WriteBackRegisters();
#endif
            }
            else
            {
                m_RegWorkingSet.WriteBackRegisters();
                MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
                MoveConstToArmReg((uint32_t)&R4300iOp::m_JumpToLocation, Arm_R1, "R4300iOp::m_JumpToLocation");
                StoreArmRegToArmRegPointer(Arm_R0, Arm_R1, 0);
            }
            OverflowDelaySlot(true);
            return;
        }

        m_Section->m_Jump.FallThrough = false;
        m_Section->m_Jump.LinkLocation = NULL;
        m_Section->m_Jump.LinkLocation2 = NULL;
        m_Section->m_Cont.FallThrough = false;
        m_Section->m_Cont.LinkLocation = NULL;
        m_Section->m_Cont.LinkLocation2 = NULL;

        if (DelaySlotEffectsCompare(m_CompilePC, m_Opcode.rs, 0))
        {
            if (IsKnown(m_Opcode.rs))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
                return;
            }
            else
            {
                MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
                MoveConstToArmReg((uint32_t)_PROGRAM_COUNTER, Arm_R1, "PROGRAM_COUNTER");
                StoreArmRegToArmRegPointer(Arm_R0, Arm_R1, 0);
            }
        }
        m_NextInstruction = DO_DELAY_SLOT;
    }
    else if (m_NextInstruction == DELAY_SLOT_DONE)
    {
        if (DelaySlotEffectsCompare(m_CompilePC, m_Opcode.rs, 0))
        {
            CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet, CExitInfo::Normal);
        }
        else
        {
            UpdateCounters(m_RegWorkingSet, true, true);
            if (IsKnown(m_Opcode.rs))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
                return;
            }
            else
            {
                MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
                MoveConstToArmReg((uint32_t)_PROGRAM_COUNTER, Arm_R1, "PROGRAM_COUNTER");
                StoreArmRegToArmRegPointer(Arm_R0, Arm_R1, 0);
            }
            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, CExitInfo::Normal);
            if (m_Section->m_JumpSection)
            {
                m_Section->GenerateSectionLinkage();
            }
        }
        m_NextInstruction = END_BLOCK;
    }
    else if (bHaveDebugger())
    {
        g_Notify->DisplayError(stdstr_f("WTF\n%s\nNextInstruction = %X", __FUNCTION__, m_NextInstruction).c_str());
    }
}

void CArmRecompilerOps::SPECIAL_JALR()
{
    if (m_NextInstruction == NORMAL)
    {
        if (DelaySlotEffectsCompare(m_CompilePC, m_Opcode.rs, 0) && (m_CompilePC & 0xFFC) != 0xFFC)
        {
            if (IsKnown(m_Opcode.rs))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
                return;
            }
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
            MoveConstToArmReg((uint32_t)_PROGRAM_COUNTER, Arm_R1, "PROGRAM_COUNTER");
            StoreArmRegToArmRegPointer(Arm_R0, Arm_R1, 0);
        }
        UnMap_GPR(m_Opcode.rd, false);
        MoveConstToVariable(m_CompilePC + 8, &_GPR[m_Opcode.rd].UW[0], CRegName::GPR_Lo[m_Opcode.rd]);
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            if (IsKnown(m_Opcode.rs))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
                return;
            }
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
            MoveConstToArmReg((uint32_t)&R4300iOp::m_JumpToLocation, Arm_R1, "R4300iOp::m_JumpToLocation");
            StoreArmRegToArmRegPointer(Arm_R0, Arm_R1, 0);

            m_RegWorkingSet.WriteBackRegisters();
            OverflowDelaySlot(true);
            return;
        }

        m_Section->m_Jump.FallThrough = false;
        m_Section->m_Jump.LinkLocation = NULL;
        m_Section->m_Jump.LinkLocation2 = NULL;
        m_Section->m_Cont.FallThrough = false;
        m_Section->m_Cont.LinkLocation = NULL;
        m_Section->m_Cont.LinkLocation2 = NULL;

        m_NextInstruction = DO_DELAY_SLOT;
    }
    else if (m_NextInstruction == DELAY_SLOT_DONE)
    {
        if (DelaySlotEffectsCompare(m_CompilePC, m_Opcode.rs, 0))
        {
            CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet, CExitInfo::Normal);
        }
        else
        {
            UpdateCounters(m_RegWorkingSet, true, true);
            if (IsKnown(m_Opcode.rs))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
                return;
            }
            MoveVariableToArmReg(&_GPR[m_Opcode.rs].UW[0], CRegName::GPR_Lo[m_Opcode.rs], Arm_R0);
            MoveConstToArmReg((uint32_t)_PROGRAM_COUNTER, Arm_R1, "PROGRAM_COUNTER");
            StoreArmRegToArmRegPointer(Arm_R0, Arm_R1, 0);
            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, CExitInfo::Normal);
            if (m_Section->m_JumpSection)
            {
                m_Section->GenerateSectionLinkage();
            }
        }
        m_NextInstruction = END_BLOCK;
    }
    else if (bHaveDebugger())
    {
        g_Notify->DisplayError(stdstr_f("WTF\n%s\nNextInstruction = %X", __FUNCTION__, m_NextInstruction).c_str());
    }
}

void CArmRecompilerOps::SPECIAL_SYSCALL()
{
    CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet, CExitInfo::DoSysCall);
    m_NextInstruction = END_BLOCK;
}

void CArmRecompilerOps::SPECIAL_MFLO()
{
    UnMap_GPR(m_Opcode.rd, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_MFLO, "R4300iOp32::SPECIAL_MFLO");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_MFLO, "R4300iOp::SPECIAL_MFLO");
    }
}

void CArmRecompilerOps::SPECIAL_MTLO()
{
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_MTLO, "R4300iOp32::SPECIAL_MTLO");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_MTLO, "R4300iOp::SPECIAL_MTLO");
    }
}

void CArmRecompilerOps::SPECIAL_MFHI()
{
    UnMap_GPR(m_Opcode.rd, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_MFHI, "R4300iOp32::SPECIAL_MFHI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_MFHI, "R4300iOp::SPECIAL_MFHI");
    }
}

void CArmRecompilerOps::SPECIAL_MTHI()
{
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_MTHI, "R4300iOp32::SPECIAL_MTHI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_MTHI, "R4300iOp::SPECIAL_MTHI");
    }
}

void CArmRecompilerOps::SPECIAL_DSLLV()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSLLV, "R4300iOp32::SPECIAL_DSLLV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSLLV, "R4300iOp::SPECIAL_DSLLV");
    }
}

void CArmRecompilerOps::SPECIAL_DSRLV()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSRLV, "R4300iOp32::SPECIAL_DSRLV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSRLV, "R4300iOp::SPECIAL_DSRLV");
    }
}

void CArmRecompilerOps::SPECIAL_DSRAV()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSRAV, "R4300iOp32::SPECIAL_DSRAV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSRAV, "R4300iOp::SPECIAL_DSRAV");
    }
}

void CArmRecompilerOps::SPECIAL_MULT()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_MULT, "R4300iOp32::SPECIAL_MULT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_MULT, "R4300iOp::SPECIAL_MULT");
    }
}

void CArmRecompilerOps::SPECIAL_MULTU()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_MULTU, "R4300iOp32::SPECIAL_MULTU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_MULTU, "R4300iOp::SPECIAL_MULTU");
    }
}

void CArmRecompilerOps::SPECIAL_DIV()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DIV, "R4300iOp32::SPECIAL_DIV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DIV, "R4300iOp::SPECIAL_DIV");
    }
}

void CArmRecompilerOps::SPECIAL_DIVU()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DIVU, "R4300iOp32::SPECIAL_DIVU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DIVU, "R4300iOp::SPECIAL_DIVU");
    }
}

void CArmRecompilerOps::SPECIAL_DMULT()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DMULT, "R4300iOp32::SPECIAL_DMULT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DMULT, "R4300iOp::SPECIAL_DMULT");
    }
}

void CArmRecompilerOps::SPECIAL_DMULTU()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DMULTU, "R4300iOp32::SPECIAL_DMULTU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DMULTU, "R4300iOp::SPECIAL_DMULTU");
    }
}

void CArmRecompilerOps::SPECIAL_DDIV()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DDIV, "R4300iOp32::SPECIAL_DDIV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DDIV, "R4300iOp::SPECIAL_DDIV");
    }
}

void CArmRecompilerOps::SPECIAL_DDIVU()
{
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DDIVU, "R4300iOp32::SPECIAL_DDIVU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DDIVU, "R4300iOp::SPECIAL_DDIVU");
    }
}

void CArmRecompilerOps::SPECIAL_ADD()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_ADD, "R4300iOp32::SPECIAL_ADD");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_ADD, "R4300iOp::SPECIAL_ADD");
    }
}

void CArmRecompilerOps::SPECIAL_ADDU()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_ADDU, "R4300iOp32::SPECIAL_ADDU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_ADDU, "R4300iOp::SPECIAL_ADDU");
    }
}

void CArmRecompilerOps::SPECIAL_SUB()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SUBU, "R4300iOp32::SPECIAL_SUBU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SUBU, "R4300iOp::SPECIAL_SUBU");
    }
}

void CArmRecompilerOps::SPECIAL_SUBU()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SUBU, "R4300iOp32::SPECIAL_SUBU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SUBU, "R4300iOp::SPECIAL_SUBU");
    }
}

void CArmRecompilerOps::SPECIAL_AND()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_AND, "R4300iOp32::SPECIAL_AND");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_AND, "R4300iOp::SPECIAL_AND");
    }
}

void CArmRecompilerOps::SPECIAL_OR()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_OR, "R4300iOp32::SPECIAL_OR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_OR, "R4300iOp::SPECIAL_OR");
    }
}

void CArmRecompilerOps::SPECIAL_XOR()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_XOR, "R4300iOp32::SPECIAL_XOR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_XOR, "R4300iOp::SPECIAL_XOR");
    }
}

void CArmRecompilerOps::SPECIAL_NOR()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    UnMap_GPR(m_Opcode.rs, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_NOR, "R4300iOp32::SPECIAL_NOR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_NOR, "R4300iOp::SPECIAL_NOR");
    }
}

void CArmRecompilerOps::SPECIAL_SLT()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SLT, "R4300iOp32::SPECIAL_SLT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SLT, "R4300iOp::SPECIAL_SLT");
    }
}

void CArmRecompilerOps::SPECIAL_SLTU()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_SLTU, "R4300iOp32::SPECIAL_SLTU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_SLTU, "R4300iOp::SPECIAL_SLTU");
    }
}

void CArmRecompilerOps::SPECIAL_DADD()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DADD, "R4300iOp32::SPECIAL_DADD");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DADD, "R4300iOp::SPECIAL_DADD");
    }
}

void CArmRecompilerOps::SPECIAL_DADDU()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DADDU, "R4300iOp32::SPECIAL_DADDU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DADDU, "R4300iOp::SPECIAL_DADDU");
    }
}

void CArmRecompilerOps::SPECIAL_DSUB()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSUB, "R4300iOp32::SPECIAL_DSUB");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSUB, "R4300iOp::SPECIAL_DSUB");
    }
}

void CArmRecompilerOps::SPECIAL_DSUBU()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rs, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSUBU, "R4300iOp32::SPECIAL_DSUBU");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSUBU, "R4300iOp::SPECIAL_DSUBU");
    }
}

void CArmRecompilerOps::SPECIAL_DSLL()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSLL, "R4300iOp32::SPECIAL_DSLL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSLL, "R4300iOp::SPECIAL_DSLL");
    }
}

void CArmRecompilerOps::SPECIAL_DSRL()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSRL, "R4300iOp32::SPECIAL_DSRL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSRL, "R4300iOp::SPECIAL_DSRL");
    }
}

void CArmRecompilerOps::SPECIAL_DSRA()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSRA, "R4300iOp32::SPECIAL_DSRA");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSRA, "R4300iOp::SPECIAL_DSRA");
    }
}

void CArmRecompilerOps::SPECIAL_DSLL32()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSLL32, "R4300iOp32::SPECIAL_DSLL32");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSLL32, "R4300iOp::SPECIAL_DSLL32");
    }
}

void CArmRecompilerOps::SPECIAL_DSRL32()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSRL32, "R4300iOp32::SPECIAL_DSRL32");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSRL32, "R4300iOp::SPECIAL_DSRL32");
    }
}

void CArmRecompilerOps::SPECIAL_DSRA32()
{
    UnMap_GPR(m_Opcode.rd, true);
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::SPECIAL_DSRA32, "R4300iOp32::SPECIAL_DSRA32");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::SPECIAL_DSRA32, "R4300iOp::SPECIAL_DSRA32");
    }
}

/************************** COP0 functions **************************/
void CArmRecompilerOps::COP0_MF()
{
    switch (m_Opcode.rd)
    {
    case 9: //Count
        m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() - g_System->CountPerOp());
        UpdateCounters(m_RegWorkingSet, false, true);
        m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_System->CountPerOp());
    }
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP0_MF, "R4300iOp32::COP0_MF");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP0_MF, "R4300iOp::COP0_MF");
    }
}

void CArmRecompilerOps::COP0_MT()
{
    if (IsKnown(m_Opcode.rt))
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
        return;
    }

    switch (m_Opcode.rd)
    {
    case 0: //Index
    case 2: //EntryLo0
    case 3: //EntryLo1
    case 4: //Context
    case 5: //PageMask
    case 10: //Entry Hi
    case 12: //Status
    case 13: //cause
    case 14: //EPC
    case 16: //Config
    case 18: //WatchLo
    case 19: //WatchHi
    case 28: //Tag lo
    case 29: //Tag Hi
    case 30: //ErrEPC
        if (g_Settings->LoadBool(Game_32Bit))
        {
            CompileInterpterCall((void *)R4300iOp32::COP0_MT, "R4300iOp32::COP0_MT");
        }
        else
        {
            CompileInterpterCall((void *)R4300iOp::COP0_MT, "R4300iOp::COP0_MT");
        }
        break;
    case 6: //Wired
    case 9: //Count
    case 11: //Compare
        m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() - g_System->CountPerOp());
        UpdateCounters(m_RegWorkingSet, false, true);
        m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_System->CountPerOp());
        if (g_Settings->LoadBool(Game_32Bit))
        {
            CompileInterpterCall((void *)R4300iOp32::COP0_MT, "R4300iOp32::COP0_MT");
        }
        else
        {
            CompileInterpterCall((void *)R4300iOp::COP0_MT, "R4300iOp::COP0_MT");
        }
        break;
    default:
        UnknownOpcode();
    }
}

void CArmRecompilerOps::COP0_CO_TLBR()
{
    if (!g_System->bUseTlb()) { return; }
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP0_CO_TLBR, "R4300iOp32::COP0_CO_TLBR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP0_CO_TLBR, "R4300iOp::COP0_CO_TLBR");
    }
}

void CArmRecompilerOps::COP0_CO_TLBWI()
{
    if (!g_System->bUseTlb()) { return; }
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP0_CO_TLBWI, "R4300iOp32::COP0_CO_TLBWI");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP0_CO_TLBWI, "R4300iOp::COP0_CO_TLBWI");
    }
}

void CArmRecompilerOps::COP0_CO_TLBWR()
{
    if (!g_System->bUseTlb()) { return; }
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP0_CO_TLBWR, "R4300iOp32::COP0_CO_TLBWR");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP0_CO_TLBWR, "R4300iOp::COP0_CO_TLBWR");
    }
}

void CArmRecompilerOps::COP0_CO_TLBP()
{
    if (!g_System->bUseTlb()) { return; }
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP0_CO_TLBP, "R4300iOp32::COP0_CO_TLBP");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP0_CO_TLBP, "R4300iOp::COP0_CO_TLBP");
    }
}

void arm_compiler_COP0_CO_ERET()
{
    if ((g_Reg->STATUS_REGISTER & STATUS_ERL) != 0)
    {
        g_Reg->m_PROGRAM_COUNTER = g_Reg->ERROREPC_REGISTER;
        g_Reg->STATUS_REGISTER &= ~STATUS_ERL;
    }
    else
    {
        g_Reg->m_PROGRAM_COUNTER = g_Reg->EPC_REGISTER;
        g_Reg->STATUS_REGISTER &= ~STATUS_EXL;
    }
    g_Reg->m_LLBit = 0;
    g_Reg->CheckInterrupts();
}

void CArmRecompilerOps::COP0_CO_ERET()
{
    m_RegWorkingSet.WriteBackRegisters();
    CallFunction((void *)arm_compiler_COP0_CO_ERET, "arm_compiler_COP0_CO_ERET");

    UpdateCounters(m_RegWorkingSet, true, true);
    CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet, CExitInfo::Normal);
    m_NextInstruction = END_BLOCK;
}

/************************** COP1 functions **************************/
void CArmRecompilerOps::COP1_MF()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_MF, "R4300iOp32::COP1_MF");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_MF, "R4300iOp::COP1_MF");
    }
}

void CArmRecompilerOps::COP1_DMF()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_DMF, "R4300iOp32::COP1_DMF");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_DMF, "R4300iOp::COP1_DMF");
    }
}

void CArmRecompilerOps::COP1_CF()
{
    CompileCop1Test();

    if (m_Opcode.fs != 31 && m_Opcode.fs != 0)
    {
        UnknownOpcode();
        return;
    }
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_CF, "R4300iOp32::COP1_CF");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_CF, "R4300iOp::COP1_CF");
    }
}

void CArmRecompilerOps::COP1_MT()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_MT, "R4300iOp32::COP1_MT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_MT, "R4300iOp::COP1_MT");
    }
}

void CArmRecompilerOps::COP1_DMT()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_DMT, "R4300iOp32::COP1_DMT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_DMT, "R4300iOp::COP1_DMT");
    }
}

void CArmRecompilerOps::COP1_CT()
{
    CompileCop1Test();

    if (m_Opcode.fs != 31)
    {
        UnknownOpcode();
        return;
    }
    UnMap_GPR(m_Opcode.rt, true);
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_CT, "R4300iOp32::COP1_CT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_CT, "R4300iOp::COP1_CT");
    }
}

void CArmRecompilerOps::COP1_S_ADD()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_ADD, "R4300iOp32::COP1_S_ADD");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_ADD, "R4300iOp::COP1_S_ADD");
    }
}

void CArmRecompilerOps::COP1_S_SUB()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_SUB, "R4300iOp32::COP1_S_SUB");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_SUB, "R4300iOp::COP1_S_SUB");
    }
}

void CArmRecompilerOps::COP1_S_MUL()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_MUL, "R4300iOp32::COP1_S_MUL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_MUL, "R4300iOp::COP1_S_MUL");
    }
}

void CArmRecompilerOps::COP1_S_DIV()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_DIV, "R4300iOp32::COP1_S_DIV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_DIV, "R4300iOp::COP1_S_DIV");
    }
}

void CArmRecompilerOps::COP1_S_ABS()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_ABS, "R4300iOp32::COP1_S_ABS");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_ABS, "R4300iOp::COP1_S_ABS");
    }
}

void CArmRecompilerOps::COP1_S_NEG()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_NEG, "R4300iOp32::COP1_S_NEG");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_NEG, "R4300iOp::COP1_S_NEG");
    }
}

void CArmRecompilerOps::COP1_S_SQRT()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_SQRT, "R4300iOp32::COP1_S_SQRT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_SQRT, "R4300iOp::COP1_S_SQRT");
    }
}

void CArmRecompilerOps::COP1_S_MOV()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_MOV, "R4300iOp32::COP1_S_MOV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_MOV, "R4300iOp::COP1_S_MOV");
    }
}

void CArmRecompilerOps::COP1_S_ROUND_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_ROUND_L, "R4300iOp32::COP1_S_ROUND_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_ROUND_L, "R4300iOp::COP1_S_ROUND_L");
    }
}

void CArmRecompilerOps::COP1_S_TRUNC_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_TRUNC_L, "R4300iOp32::COP1_S_TRUNC_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_TRUNC_L, "R4300iOp::COP1_S_TRUNC_L");
    }
}

void CArmRecompilerOps::COP1_S_CEIL_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_CEIL_L, "R4300iOp32::COP1_S_CEIL_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_CEIL_L, "R4300iOp::COP1_S_CEIL_L");
    }
}

void CArmRecompilerOps::COP1_S_FLOOR_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_FLOOR_L, "R4300iOp32::COP1_S_FLOOR_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_FLOOR_L, "R4300iOp::COP1_S_FLOOR_L");
    }
}

void CArmRecompilerOps::COP1_S_ROUND_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_ROUND_W, "R4300iOp32::COP1_S_ROUND_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_ROUND_W, "R4300iOp::COP1_S_ROUND_W");
    }
}

void CArmRecompilerOps::COP1_S_TRUNC_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_TRUNC_W, "R4300iOp32::COP1_S_TRUNC_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_TRUNC_W, "R4300iOp::COP1_S_TRUNC_W");
    }
}

void CArmRecompilerOps::COP1_S_CEIL_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_CEIL_W, "R4300iOp32::COP1_S_CEIL_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_CEIL_W, "R4300iOp::COP1_S_CEIL_W");
    }
}

void CArmRecompilerOps::COP1_S_FLOOR_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_FLOOR_W, "R4300iOp32::COP1_S_FLOOR_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_FLOOR_W, "R4300iOp::COP1_S_FLOOR_W");
    }
}

void CArmRecompilerOps::COP1_S_CVT_D()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_CVT_D, "R4300iOp32::COP1_S_CVT_D");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_CVT_D, "R4300iOp::COP1_S_CVT_D");
    }
}

void CArmRecompilerOps::COP1_S_CVT_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_CVT_W, "R4300iOp32::COP1_S_CVT_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_CVT_W, "R4300iOp::COP1_S_CVT_W");
    }
}

void CArmRecompilerOps::COP1_S_CVT_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_CVT_L, "R4300iOp32::COP1_S_CVT_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_CVT_L, "R4300iOp::COP1_S_CVT_L");
    }
}

void CArmRecompilerOps::COP1_S_CMP()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_S_CMP, "R4300iOp32::COP1_S_CMP");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_S_CMP, "R4300iOp::COP1_S_CMP");
    }
}

void CArmRecompilerOps::COP1_D_ADD()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_ADD, "R4300iOp32::COP1_D_ADD");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_ADD, "R4300iOp::COP1_D_ADD");
    }
}

void CArmRecompilerOps::COP1_D_SUB()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_SUB, "R4300iOp32::COP1_D_SUB");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_SUB, "R4300iOp::COP1_D_SUB");
    }
}

void CArmRecompilerOps::COP1_D_MUL()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_MUL, "R4300iOp32::COP1_D_MUL");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_MUL, "R4300iOp::COP1_D_MUL");
    }
}

void CArmRecompilerOps::COP1_D_DIV()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_DIV, "R4300iOp32::COP1_D_DIV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_DIV, "R4300iOp::COP1_D_DIV");
    }
}

void CArmRecompilerOps::COP1_D_ABS()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_ABS, "R4300iOp32::COP1_D_ABS");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_ABS, "R4300iOp::COP1_D_ABS");
    }
}

void CArmRecompilerOps::COP1_D_NEG()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_NEG, "R4300iOp32::COP1_D_NEG");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_NEG, "R4300iOp::COP1_D_NEG");
    }
}

void CArmRecompilerOps::COP1_D_SQRT()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_SQRT, "R4300iOp32::COP1_D_SQRT");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_SQRT, "R4300iOp::COP1_D_SQRT");
    }
}

void CArmRecompilerOps::COP1_D_MOV()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_MOV, "R4300iOp32::COP1_D_MOV");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_MOV, "R4300iOp::COP1_D_MOV");
    }
}

void CArmRecompilerOps::COP1_D_ROUND_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_ROUND_L, "R4300iOp32::COP1_D_ROUND_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_ROUND_L, "R4300iOp::COP1_D_ROUND_L");
    }
}

void CArmRecompilerOps::COP1_D_TRUNC_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_TRUNC_L, "R4300iOp32::COP1_D_TRUNC_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_TRUNC_L, "R4300iOp::COP1_D_TRUNC_L");
    }
}

void CArmRecompilerOps::COP1_D_CEIL_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_CEIL_L, "R4300iOp32::COP1_D_CEIL_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_CEIL_L, "R4300iOp::COP1_D_CEIL_L");
    }
}

void CArmRecompilerOps::COP1_D_FLOOR_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_FLOOR_L, "R4300iOp32::COP1_D_FLOOR_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_FLOOR_L, "R4300iOp::COP1_D_FLOOR_L");
    }
}

void CArmRecompilerOps::COP1_D_ROUND_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_ROUND_W, "R4300iOp32::COP1_D_ROUND_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_ROUND_W, "R4300iOp::COP1_D_ROUND_W");
    }
}

void CArmRecompilerOps::COP1_D_TRUNC_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_TRUNC_W, "R4300iOp32::COP1_D_TRUNC_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_TRUNC_W, "R4300iOp::COP1_D_TRUNC_W");
    }
}

void CArmRecompilerOps::COP1_D_CEIL_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_CEIL_W, "R4300iOp32::COP1_D_CEIL_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_CEIL_W, "R4300iOp::COP1_D_CEIL_W");
    }
}

void CArmRecompilerOps::COP1_D_FLOOR_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_FLOOR_W, "R4300iOp32::COP1_D_FLOOR_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_FLOOR_W, "R4300iOp::COP1_D_FLOOR_W");
    }
}

void CArmRecompilerOps::COP1_D_CVT_S()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_CVT_S, "R4300iOp32::COP1_D_CVT_S");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_CVT_S, "R4300iOp::COP1_D_CVT_S");
    }
}

void CArmRecompilerOps::COP1_D_CVT_W()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_CVT_W, "R4300iOp32::COP1_D_CVT_W");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_CVT_W, "R4300iOp::COP1_D_CVT_W");
    }
}

void CArmRecompilerOps::COP1_D_CVT_L()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_CVT_L, "R4300iOp32::COP1_D_CVT_L");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_CVT_L, "R4300iOp::COP1_D_CVT_L");
    }
}

void CArmRecompilerOps::COP1_D_CMP()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_D_CMP, "R4300iOp32::COP1_D_CMP");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_D_CMP, "R4300iOp::COP1_D_CMP");
    }
}

void CArmRecompilerOps::COP1_W_CVT_S()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_W_CVT_S, "R4300iOp32::COP1_W_CVT_S");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_W_CVT_S, "R4300iOp::COP1_W_CVT_S");
    }
}

void CArmRecompilerOps::COP1_W_CVT_D()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_W_CVT_D, "R4300iOp32::COP1_W_CVT_D");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_W_CVT_D, "R4300iOp::COP1_W_CVT_D");
    }
}

void CArmRecompilerOps::COP1_L_CVT_S()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_L_CVT_S, "R4300iOp32::COP1_L_CVT_S");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_L_CVT_S, "R4300iOp::COP1_L_CVT_S");
    }
}

void CArmRecompilerOps::COP1_L_CVT_D()
{
    CompileCop1Test();
    if (g_Settings->LoadBool(Game_32Bit))
    {
        CompileInterpterCall((void *)R4300iOp32::COP1_L_CVT_D, "R4300iOp32::COP1_L_CVT_D");
    }
    else
    {
        CompileInterpterCall((void *)R4300iOp::COP1_L_CVT_D, "R4300iOp::COP1_L_CVT_D");
    }
}

void CArmRecompilerOps::UnknownOpcode()
{
    m_RegWorkingSet.WriteBackRegisters();
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() - g_System->CountPerOp());
    UpdateCounters(m_RegWorkingSet, false, true);
    MoveConstToVariable(m_CompilePC, &g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER");
    if (g_SyncSystem)
    {
        MoveConstToArmReg((uint32_t)g_BaseSystem, Arm_R0, "g_BaseSystem");
        CallFunction(AddressOf(&CN64System::SyncSystem), "CN64System::SyncSystem");
    }

    MoveConstToVariable(m_Opcode.Hex, &R4300iOp::m_Opcode.Hex, "R4300iOp::m_Opcode.Hex");
    CallFunction((void *)R4300iOp::UnknownOpcode, "R4300iOp::UnknownOpcode");
    ExitCodeBlock();
    if (m_NextInstruction == NORMAL) { m_NextInstruction = END_BLOCK; }
}

void CArmRecompilerOps::EnterCodeBlock()
{
    PushArmReg(ArmPushPop_R3 | ArmPushPop_R4 | ArmPushPop_R5 | ArmPushPop_R6 | ArmPushPop_R7 | ArmPushPop_LR);
}

void CArmRecompilerOps::ExitCodeBlock()
{
    if (g_SyncSystem)
    {
        MoveConstToArmReg((uint32_t)g_BaseSystem, Arm_R0, "g_BaseSystem");
        CallFunction(AddressOf(&CN64System::SyncSystem), "CN64System::SyncSystem");
    }
    PopArmReg(ArmPushPop_R3 | ArmPushPop_R4 | ArmPushPop_R5 | ArmPushPop_R6 | ArmPushPop_R7 | ArmPushPop_PC);
}

void CArmRecompilerOps::CompileExitCode()
{
    for (EXIT_LIST::iterator ExitIter = m_ExitInfo.begin(); ExitIter != m_ExitInfo.end(); ExitIter++)
    {
        CPU_Message("");
        CPU_Message("      $Exit_%d", ExitIter->ID);
        SetJump20(ExitIter->JumpLoc, (uint32_t *)*g_RecompPos);
        m_NextInstruction = ExitIter->NextInstruction;
        CompileExit((uint32_t)-1, ExitIter->TargetPC, ExitIter->ExitRegSet, ExitIter->reason);
    }
}

void CArmRecompilerOps::CompileCop1Test()
{
    if (m_RegWorkingSet.GetFpuBeenUsed())
        return;

    MoveVariableToArmReg(&g_Reg->STATUS_REGISTER, "STATUS_REGISTER", Arm_R1);
    MoveConstToArmReg(STATUS_CU1, Arm_R2, "STATUS_REGISTER");
    AndArmRegToArmReg(Arm_R2, Arm_R1);
    CompareArmRegToConst(Arm_R1, 0);
    CompileExit(m_CompilePC, m_CompilePC, m_RegWorkingSet, CExitInfo::COP1_Unuseable, ArmBranch_Equal);
    m_RegWorkingSet.SetFpuBeenUsed(true);
}

void CArmRecompilerOps::CompileInPermLoop(CRegInfo & RegSet, uint32_t ProgramCounter)
{
    MoveConstToVariable(ProgramCounter, _PROGRAM_COUNTER, "PROGRAM_COUNTER");
    RegSet.WriteBackRegisters();
    UpdateCounters(RegSet, false, true);
    CallFunction(AddressOf(CInterpreterCPU::InPermLoop), "CInterpreterCPU::InPermLoop");
    MoveConstToArmReg((uint32_t)g_SystemTimer, Arm_R0);
    CallFunction(AddressOf(&CSystemTimer::TimerDone), "CSystemTimer::TimerDone");
    CPU_Message("CompileSystemCheck 3");
    CompileSystemCheck((uint32_t)-1, RegSet);
    if (g_SyncSystem)
    {
        MoveConstToArmReg((uint32_t)g_BaseSystem, Arm_R0);
        CallFunction(AddressOf(&CN64System::SyncSystem), "CN64System::SyncSystem");
    }
}

void CArmRecompilerOps::SyncRegState(const CRegInfo & SyncTo)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CArmRecompilerOps::CompileExit(uint32_t JumpPC, uint32_t TargetPC, CRegInfo &ExitRegSet, CExitInfo::EXIT_REASON reason)
{
    if (TargetPC != (uint32_t)-1)
    {
        MoveConstToArmReg(TargetPC, Arm_R1);
        MoveConstToArmReg((uint32_t)&g_Reg->m_PROGRAM_COUNTER, Arm_R2, "PROGRAM_COUNTER");
        StoreArmRegToArmRegPointer(Arm_R1, Arm_R2, 0);

        UpdateCounters(ExitRegSet, TargetPC <= JumpPC && JumpPC != -1, reason == CExitInfo::Normal);
    }
    else
    {
        UpdateCounters(ExitRegSet, false, reason == CExitInfo::Normal);
    }

    bool bDelay;
    switch (reason)
    {
    case CExitInfo::Normal:
    case CExitInfo::Normal_NoSysCheck:
        ExitRegSet.SetBlockCycleCount(0);
        if (TargetPC != (uint32_t)-1)
        {
            if (TargetPC <= JumpPC && reason == CExitInfo::Normal)
            {
                CPU_Message("CompileSystemCheck 1");
                CompileSystemCheck((uint32_t)-1, ExitRegSet);
            }
        }
        else
        {
            if (reason == CExitInfo::Normal)
            {
                CPU_Message("CompileSystemCheck 2");
                CompileSystemCheck((uint32_t)-1, ExitRegSet);
            }
        }
        ExitCodeBlock();
        break;
    case CExitInfo::DoSysCall:
        bDelay = m_NextInstruction == JUMP || m_NextInstruction == DELAY_SLOT;
        MoveConstToArmReg((uint32_t)bDelay, Arm_R1, bDelay ? "true" : "false");
        MoveConstToArmReg((uint32_t)g_Reg, Arm_R0);
        CallFunction(AddressOf(&CRegisters::DoSysCallException), "CRegisters::DoSysCallException");
        ExitCodeBlock();
        break;
    case CExitInfo::COP1_Unuseable:
        bDelay = m_NextInstruction == JUMP || m_NextInstruction == DELAY_SLOT;
        MoveConstToArmReg((uint32_t)1, Arm_R2, "1");
        MoveConstToArmReg((uint32_t)bDelay, Arm_R1, bDelay ? "true" : "false");
        MoveConstToArmReg((uint32_t)g_Reg, Arm_R0);
        CallFunction(AddressOf(&CRegisters::DoCopUnusableException), "CRegisters::DoCopUnusableException");
        ExitCodeBlock();
        break;
    default:
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CArmRecompilerOps::CompileExit(uint32_t JumpPC, uint32_t TargetPC, CRegInfo &ExitRegSet, CExitInfo::EXIT_REASON reason, CArmOps::ArmBranchCompare CompareType)
{
    BranchLabel20(CompareType, stdstr_f("Exit_%d", m_ExitInfo.size()).c_str());

    CExitInfo ExitInfo;
    ExitInfo.ID = m_ExitInfo.size();
    ExitInfo.TargetPC = TargetPC;
    ExitInfo.ExitRegSet = ExitRegSet;
    ExitInfo.reason = reason;
    ExitInfo.NextInstruction = m_NextInstruction;
    ExitInfo.JumpLoc = (uint32_t *)(*g_RecompPos - 4);
    m_ExitInfo.push_back(ExitInfo);
}

void CArmRecompilerOps::CompileSystemCheck(uint32_t TargetPC, const CRegInfo & RegSet)
{
    MoveVariableToArmReg((void *)&g_SystemEvents->DoSomething(), "g_SystemEvents->DoSomething()", Arm_R2);
    CompareArmRegToConst(Arm_R2, 0);
    BranchLabel20(ArmBranch_Equal, "Continue_From_Interrupt_Test");
    uint32_t * Jump = (uint32_t *)(*g_RecompPos - 4);

    if (TargetPC != (uint32_t)-1)
    {
        MoveConstToVariable(TargetPC, &g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER");
    }

    CRegInfo RegSetCopy(RegSet);
    RegSetCopy.WriteBackRegisters();

    MoveConstToArmReg((uint32_t)g_SystemEvents, Arm_R0, "g_SystemEvents");
    CallFunction(AddressOf(&CSystemEvents::ExecuteEvents), "CSystemEvents::ExecuteEvents");
    ExitCodeBlock();
    CPU_Message("");
    CPU_Message("      $Continue_From_Interrupt_Test:");
    SetJump20(Jump, (uint32_t *)*g_RecompPos);
}

CRegInfo & CArmRecompilerOps::GetRegWorkingSet(void)
{
    return m_RegWorkingSet;
}

void CArmRecompilerOps::SetRegWorkingSet(const CRegInfo & RegInfo)
{
    m_RegWorkingSet = RegInfo;
}

bool CArmRecompilerOps::InheritParentInfo()
{
    if (m_Section->m_CompiledLocation == NULL)
    {
        m_Section->m_CompiledLocation = *g_RecompPos;
        m_Section->DisplaySectionInformation();
        m_Section->m_CompiledLocation = NULL;
    }
    else
    {
        m_Section->DisplaySectionInformation();
    }

    if (m_Section->m_ParentSection.empty())
    {
        SetRegWorkingSet(m_Section->m_RegEnter);
        return true;
    }

    if (m_Section->m_ParentSection.size() == 1)
    {
        CCodeSection * Parent = *(m_Section->m_ParentSection.begin());
        if (Parent->m_CompiledLocation == NULL)
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
        CJumpInfo * JumpInfo = m_Section == Parent->m_ContinueSection ? &Parent->m_Cont : &Parent->m_Jump;

        m_Section->m_RegEnter = JumpInfo->RegSet;
        LinkJump(*JumpInfo, m_Section->m_SectionID);
        SetRegWorkingSet(m_Section->m_RegEnter);
        return true;
    }

    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CArmRecompilerOps::LinkJump(CJumpInfo & JumpInfo, uint32_t SectionID, uint32_t FromSectionID)
{
    if (JumpInfo.LinkLocation != NULL)
    {
        if (SectionID != -1)
        {
            if (FromSectionID != -1)
            {
                CPU_Message("   Section_%d (from %d):", SectionID, FromSectionID);
            }
            else
            {
                CPU_Message("   Section_%d:", SectionID);
            }
        }
        SetJump20(JumpInfo.LinkLocation, (uint32_t *)*g_RecompPos);
        JumpInfo.LinkLocation = NULL;
        if (JumpInfo.LinkLocation2 != NULL)
        {
            SetJump20(JumpInfo.LinkLocation2, (uint32_t *)*g_RecompPos);
            JumpInfo.LinkLocation2 = NULL;
        }
    }
}

void CArmRecompilerOps::JumpToSection(CCodeSection * Section)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CArmRecompilerOps::JumpToUnknown(CJumpInfo * JumpInfo)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CArmRecompilerOps::SetCurrentPC(uint32_t ProgramCounter)
{
    m_CompilePC = ProgramCounter;
    __except_try()
    {
        if (!g_MMU->LW_VAddr(m_CompilePC, m_Opcode.Hex))
        {
            g_Notify->FatalError(GS(MSG_FAIL_LOAD_WORD));
        }
    }
    __except_catch()
    {
        g_Notify->FatalError(GS(MSG_UNKNOWN_MEM_ACTION));
    }
}

uint32_t CArmRecompilerOps::GetCurrentPC(void)
{
    return m_CompilePC;
}

void CArmRecompilerOps::SetCurrentSection(CCodeSection * section)
{
    m_Section = section;
}

void CArmRecompilerOps::SetNextStepType(STEP_TYPE StepType)
{
    m_NextInstruction = StepType;
}

STEP_TYPE CArmRecompilerOps::GetNextStepType(void)
{
    return m_NextInstruction;
}

const OPCODE &CArmRecompilerOps::GetOpcode(void) const
{
    return m_Opcode;
    g_Notify->BreakPoint(__FILE__, __LINE__);
}

void CArmRecompilerOps::UpdateSyncCPU(CRegInfo & RegSet, uint32_t Cycles)
{
    if (!g_SyncSystem)
    {
        return;
    }
    WriteArmComment("Updating Sync CPU");
    RegSet.BeforeCallDirect();
    MoveConstToArmReg(Cycles, Arm_R2);
    MoveConstToArmReg((uint32_t)g_SyncSystem, Arm_R1, "g_SyncSystem");
    MoveConstToArmReg((uint32_t)g_System, Arm_R0);
    CallFunction((void *)AddressOf(&CN64System::UpdateSyncCPU), "CN64System::UpdateSyncCPU");
    RegSet.AfterCallDirect();
}

void CArmRecompilerOps::UpdateCounters(CRegInfo & RegSet, bool CheckTimer, bool ClearValues)
{
    if (RegSet.GetBlockCycleCount() != 0)
    {
        UpdateSyncCPU(RegSet, RegSet.GetBlockCycleCount());
        WriteArmComment("Update Counter");
        SubConstFromVariable(RegSet.GetBlockCycleCount(), g_NextTimer, "g_NextTimer"); // updates compare flag
        if (ClearValues)
        {
            RegSet.SetBlockCycleCount(0);
        }
    }

    if (CheckTimer)
    {
        MoveVariableToArmReg(g_NextTimer, "g_NextTimer", Arm_R2);
        CompareArmRegToConst(Arm_R2, 0);
        uint8_t * Jump = *g_RecompPos;
        BranchLabel8(ArmBranch_GreaterThanOrEqual, "Continue_From_Timer_Test");
        RegSet.BeforeCallDirect();
        MoveConstToArmReg((uint32_t)g_SystemTimer, Arm_R0, "g_SystemTimer");
        CallFunction(AddressOf(&CSystemTimer::TimerDone), "CSystemTimer::TimerDone");
        RegSet.AfterCallDirect();

        CPU_Message("");
        CPU_Message("      $Continue_From_Timer_Test:");
        SetJump8(Jump, *g_RecompPos);
    }
}

void CArmRecompilerOps::CompileInterpterCall(void * Function, const char * FunctionName)
{
    MoveConstToVariable(m_Opcode.Hex, (void *)&R4300iOp::m_Opcode.Hex, "&R4300iOp::m_Opcode.Hex");
    m_RegWorkingSet.BeforeCallDirect();
    CallFunction(Function, FunctionName);
    m_RegWorkingSet.AfterCallDirect();
}

void CArmRecompilerOps::OverflowDelaySlot(bool TestTimer)
{
    m_RegWorkingSet.WriteBackRegisters();
    UpdateCounters(m_RegWorkingSet, false, true);
    MoveConstToVariable(m_CompilePC + 4, _PROGRAM_COUNTER, "PROGRAM_COUNTER");

    if (g_SyncSystem)
    {
        MoveConstToArmReg((uint32_t)g_BaseSystem, Arm_R0, "g_BaseSystem");
        CallFunction(AddressOf(&CN64System::SyncSystem), "CN64System::SyncSystem");
    }

    MoveConstToVariable(JUMP, &R4300iOp::m_NextInstruction, "R4300iOp::m_NextInstruction");

    if (TestTimer)
    {
        MoveConstToVariable(TestTimer, &R4300iOp::m_TestTimer, "R4300iOp::m_TestTimer");
    }

    MoveConstToArmReg(g_System->CountPerOp(), Arm_R0);
    CallFunction((void *)CInterpreterCPU::ExecuteOps, "CInterpreterCPU::ExecuteOps");

    if (g_System->bFastSP() && g_Recompiler)
    {
        MoveConstToArmReg((uint32_t)g_Recompiler, Arm_R0);
        CallFunction(AddressOf(&CRecompiler::ResetMemoryStackPos), "CRecompiler::ResetMemoryStackPos");
    }
    if (g_SyncSystem)
    {
        UpdateSyncCPU(m_RegWorkingSet, g_System->CountPerOp());
    }

    ExitCodeBlock();
    m_NextInstruction = END_BLOCK;
}

#endif