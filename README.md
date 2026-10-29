# Energy Monitoring System - Energy Calculation & Handover Guide

This document explains the methodology used by the **ELFORGE Energy Monitoring System** to calculate energy consumption, perform shift-wise reporting, and operate the interactive dashboard.

> For complete documentation including API endpoints and architecture, see [UPDATE_README.md](file:///c:/Users/vakav/virtualpy/elforge_updating/UPDATE_README.md).

---

## 1. Overview

The system tracks **Cumulative Energy (kWh)** directly from the **MFM384 Energy Meter**. This value is a running total that does not reset daily. To determine how much energy was used during a specific timeframe (like a shift), the system calculates the **increase** between readings.

---

## 2. Calculation Methodology

The system uses a **Delta Accumulation** approach to ensure accuracy even if there are network outages or data gaps.

### The Algorithm:

1. **Sort Records**: All records for the selected date range and shift are sorted in chronological order (oldest to newest).
2. **Calculate Delta**: For every consecutive pair of readings:
   $$\text{Delta} = \text{Reading}_{current} - \text{Reading}_{previous}$$
3. **Filter Anomalies**:
   - If **$0 < \text{Delta} < 10000$**: The energy increased (power was consumed). This delta is added to the running total and attributed to the current reading's shift.
   - If **$\text{Delta} \le 0$ or $\text{Delta} \ge 10000$**: This is ignored to prevent corrupted totals from meter resets or communication glitches.
4. **Final Result**: The sum of all valid Deltas is the total energy consumed during that period.

---

## 3. Shift Breakdown

Energy consumption is automatically attributed to a shift based on the **Timestamp** of the reading.

| Shift | Start Time | End Time |
| :--- | :--- | :--- |
| **Shift A** (Morning) | 06:30 (06:30 AM) | 14:29:59 (02:29 PM) |
| **Shift B** (Evening) | 14:30 (02:30 PM) | 22:29:59 (10:29 PM) |
| **Shift C** (Night) | 22:30 (10:30 PM) | 06:29:59 (06:29 AM) |

> [!NOTE]
> Consumption is attributed to the shift in which the **End** of the consumption interval occurred. For example, if energy is consumed between 06:29 and 06:31, that energy will be counted towards **Shift A**.

---

## 4. Where to Find Calculations in the Application

### 1. Live Dashboard (`/dashboard`)
* **Shift Consumption Cards**: Dedicated KPI cards for Shift A, Shift B, Shift C, and Total Consumed kWh.
* **Active Shift Indicator**: Displays the current active shift badge based on real-time server time.
* **Shift Distribution Chart**: Visual bar chart comparing Shift A, B, and C consumption.
* **Consumption Timeline Chart**: Interactive trend graph showing interval consumption over time.
* **Table Summary Footer**: At the bottom of the data table, the `<tfoot>` summarizes Total Period Consumption and individual shift totals.

### 2. Shift Analytics & Reports Hub (`/reports`)
* Comprehensive shift-level reporting dashboard with customizable date ranges and quick presets.
* **Daily Shift Stacked Bar Chart** and **Shift Share Doughnut Chart**.
* Full date-wise breakdown table with shift totals and % shares.
* Direct **CSV** and **Excel** export downloads.

### 3. CSV & Excel Export
* Detailed reports sectioned by machine with per-slave shift breakdowns and global shift summaries.

---

_Designed & Developed by Vel Tech University, Chennai & Advanced Energy Monitoring Systems_
