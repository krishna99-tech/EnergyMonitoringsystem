# Energy Monitoring System - Energy Calculation Guide

This document explains the methodology used by the **ELFORGE Energy Monitoring System** to calculate energy consumption and perform shift-wise reporting.

## 1. Overview

The system tracks **Cumulative Energy (kWh)** directly from the **MFM384 Energy Meter**. This value is a running total that does not reset daily. To determine how much energy was used during a specific timeframe (like a shift), the system must calculate the **increase** between readings.

## 2. Calculation Methodology

The system uses a **Delta Accumulation** approach to ensure accuracy even if there are network outages or data gaps.

### The Algorithm:

1. **Sort Records**: All records for the selected date range and shift are sorted in chronological order (oldest to newest).
2. **Calculate Delta**: For every consecutive pair of readings:
   $$\text{Delta} = \text{Reading}_{current} - \text{Reading}_{previous}$$
3. **Filter Anomalies**:
   - If **Delta > 0**: The energy increased (power was consumed). This delta is added to the running total.
   - If **Delta ≤ 0**: This is ignored. (This prevents errors if the meter replaces its internal battery/counter or if a reading is skipped).
4. **Final Result**: The sum of all valid Deltas is the total energy consumed during that period.

## 3. Shift Breakdown

Energy consumption is automatically attributed to a shift based on the **Timestamp** of the reading.

| Shift       | Start Time       | End Time         |
| :---------- | :--------------- | :--------------- |
| **Shift A** | 06:00 (06:00 AM) | 13:59 (01:59 PM) |
| **Shift B** | 14:00 (02:00 PM) | 21:59 (09:59 PM) |
| **Shift C** | 22:00 (10:00 PM) | 05:59 (05:59 AM) |

> [!NOTE]
> Consumption is attributed to the shift in which the **End** of the consumption interval occurred. For example, if energy is consumed between 05:59 and 06:01, that energy will be counted towards **Shift A**.

## 4. Where to find Calculations

You can view these calculations in two main areas of the application:

### Dashboard Results Table

At the bottom of the data table, a **Table Footer** summarizes the current view:

- **Total Period Consumption**: Sum of all energy usage across all visible shifts in the table.
- **Shift A/B/C Consumption**: Individual totals specifically for each workforce shift.

### CSV Export (Reports)

When you click **Download CSV**, the generated file will contain a **SUMMARY** section at the bottom. This allows management to quickly see the total kWh and shift-wide efficiency without manually calculating from the raw data rows.

---

_Designed & Developed by Vel Tech University, Chennai_
