import asyncio
from asyncio.tasks import Task
import collections
import tkinter
from tkinter import ttk
from tkinter import messagebox
from tkinter.constants import FALSE
import sys

import bleak
from bleak.exc import *
from bleak.backends.client import BaseBleakClient
from bleak_winrt.windows.foundation import HResult
import matplotlib
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy
import re

titleText = "OpenTracker (WIP)"
diagTitle = "Select Device"
connText = "Connected to: {device}"
cmdChoice = ("Reset", "Calibration")
devAddrFormat = "{dev_name} | {uuid}"
uuid_base = "32e6{uuid16}-2b22-4db5-a914-43ce41986c70"
srv_uuid = uuid_base.format(uuid16="1089")
cmd_uuid = uuid_base.format(uuid16="108a")
max_red_uuid = uuid_base.format(uuid16="108b")
max_ir_uuid = uuid_base.format(uuid16="108c")
gsr_uuid = uuid_base.format(uuid16="108d")
flex_uuid = uuid_base.format(uuid16="108e")
emg1_uuid = uuid_base.format(uuid16="108f")
emg2_uuid = uuid_base.format(uuid16="1090")

class OpenTrackerApp:
    def __init__(self) -> None:
        self.GSR = collections.deque([0] * 256, maxlen=256)
        self.Flex = collections.deque([0] * 256, maxlen=256)
        self.HR_Red = collections.deque([0] * 256, maxlen=256)
        self.HR_IR = collections.deque([0] * 256, maxlen=256)
        self.EMG1 = collections.deque([0] * 256, maxlen=256)
        self.EMG2 = collections.deque([0] * 256, maxlen=256)
        # ^ data
        self.alive : bool = True
        self.scanUpdate : bool = False
        self.mainWindow = tkinter.Tk()
        self.mainWindow.title(titleText)
        self.mainWindow.resizable(FALSE, FALSE)
        self.mainWindow.protocol("WM_DELETE_WINDOW", onClose)
        self.frm = ttk.Frame(self.mainWindow)
        self.frm.grid(row=0, column=0)
        self.connectBtn = ttk.Button(self.frm, text="Connect", command=createDialog)
        self.connectBtn.grid(row=0, column=0)
        self.connectDiag : tkinter.Tk = None
        self.connectSelector : tkinter.Listbox = None
        self.connectSelectorList : tkinter.StringVar
        self.connectSelectorBtn : ttk.Button = None
        self.discBtn = ttk.Button(self.frm, text="Disconnect", command=lambda: self.evloop.create_task(disconnectWrapper()))
        self.discBtn.state(["disabled"])
        self.discBtn.grid(row=0, column=1)
        self.BLEDev : BaseBleakClient = None
        self.evloop = asyncio.get_event_loop()
        self.idletask = self.evloop.create_task(idletask(), name="idletask")
        self.connectionStatusTxt = tkinter.StringVar(value=connText.format(device="None" if self.BLEDev == None else self.BLEDev.address))
        self.connectionStatus = ttk.Label(self.frm, textvariable=self.connectionStatusTxt)
        self.connectionStatus.grid(row=0, column=2, columnspan=2)
        self.ctrlPane = ttk.Frame(self.frm, padding=10)
        self.ctrlPane.grid(row=0, column=4, columnspan=3)
        self.selectedCmd = tkinter.StringVar()
        self.cmdSelector = ttk.Combobox(self.ctrlPane, textvariable=self.selectedCmd)
        self.cmdSelector.state(["readonly"])
        self.cmdSelector["values"] = cmdChoice
        self.cmdSelector.grid(row=0, column=4, columnspan=2)
        self.cmdSendBtn = ttk.Button(self.ctrlPane, text="Send", command=lambda: self.sendCmd())
        self.cmdSendBtn.state(["disabled"])
        self.cmdSendBtn.grid(row=0, column=6)
        #
        self.graph_gsr = Figure(figsize=(8, 4))
        self.t_gsr = numpy.arange(-255, 1, 1)
        self.ax_gsr = self.graph_gsr.add_subplot()
        self.ax_gsr.set_ylim(ymin=0, ymax=1024, auto=True)
        self.line_gsr, = self.ax_gsr.plot(self.t_gsr, numpy.array(list(self.GSR)))
        self.ax_gsr.set_xlabel("GSR")
        #
        self.graph_flex = Figure(figsize=(8, 4))
        self.t_flex = numpy.arange(-255, 1, 1)
        self.ax_flex = self.graph_flex.add_subplot()
        self.ax_flex.set_ylim(ymin=0, ymax=1024, auto=True)
        self.line_flex, = self.ax_flex.plot(self.t_flex, numpy.array(list(self.Flex)))
        self.ax_flex.set_xlabel("Flex")
        #
        self.graph_emg = Figure(figsize=(8, 4))
        self.t_emg = numpy.arange(-255, 1, 1)
        self.ax_emg = self.graph_emg.add_subplot()
        self.ax_emg.set_ylim(ymin=0, ymax=1024, auto=True)
        self.line_emg1, = self.ax_emg.plot(self.t_emg, numpy.array(list(self.EMG1)))
        self.line_emg2, = self.ax_emg.plot(self.t_emg, numpy.array(list(self.EMG2)))
        self.ax_emg.set_xlabel("EMG")
        #
        self.graph_hr = Figure(figsize=(8, 4))
        self.t_hr = numpy.arange(-255, 1, 1)
        self.ax_hr = self.graph_hr.add_subplot()
        self.ax_hr.set_ylim(ymin=0, ymax=1024, auto=True)
        self.line_hr_red, = self.ax_hr.plot(self.t_hr, numpy.array(list(self.HR_Red)))
        self.line_hr_ir, = self.ax_hr.plot(self.t_hr, numpy.array(list(self.HR_IR)))
        self.ax_hr.set_xlabel("HR")
        #
        self.graphCanvas_gsr = FigureCanvasTkAgg(self.graph_gsr, master=self.frm)
        self.graphCanvas_flex = FigureCanvasTkAgg(self.graph_flex, master=self.frm)
        self.graphCanvas_emg = FigureCanvasTkAgg(self.graph_emg, master=self.frm)
        self.graphCanvas_hr = FigureCanvasTkAgg(self.graph_hr, master=self.frm)
        self.graphCanvas_gsr.draw()
        self.graphCanvas_flex.draw()
        self.graphCanvas_emg.draw()
        self.graphCanvas_hr.draw()
        self.graphCanvas_gsr.get_tk_widget().grid(row=1, column=0, rowspan=2, columnspan=3)
        self.graphCanvas_flex.get_tk_widget().grid(row=1, column=3, rowspan=2, columnspan=3)
        self.graphCanvas_emg.get_tk_widget().grid(row=3, column=0, rowspan=2, columnspan=3)
        self.graphCanvas_hr.get_tk_widget().grid(row=3, column=3, rowspan=2, columnspan=3)
        self.update()
    def deviceScan(self) -> None:
        self.evloop.create_task(deviceScanWrapper())
        if self.scanUpdate:
            self.connectDiag.after(3000, self.deviceScan)
    def tryConnect(self, dev : str) -> None:
        # format is name | addr
        self.evloop.create_task(tryConnectWrapper(dev))
    def update(self) -> None:
        self.connectionStatusTxt.set(connText.format(device="None" if self.BLEDev == None else self.BLEDev.address))
        if self.BLEDev != None:
            self.evloop.create_task(connUpdateWrapper())
        if self.BLEDev != None:
            self.evloop.create_task(telemetryWrapper())
        self.mainWindow.after(200, self.update)
    def sendCmd(self) -> None:
        pass

appInstance : OpenTrackerApp = None

async def idletask():
    while appInstance.alive:
        appInstance.mainWindow.update()
        await asyncio.sleep(1/30)

async def canvasWorker():
    appInstance.line_hr_red.set_data(appInstance.t_hr, numpy.array(list(appInstance.HR_Red)))
    appInstance.line_hr_ir.set_data(appInstance.t_hr, numpy.array(list(appInstance.HR_IR)))
    appInstance.ax_hr.set_ylim(ymin=0, ymax=1024, auto=True)
    appInstance.graphCanvas_hr.draw()
    appInstance.line_gsr.set_data(appInstance.t_gsr, numpy.array(list(appInstance.GSR)))
    appInstance.ax_gsr.set_ylim(ymin=0, ymax=1024, auto=True)
    appInstance.graphCanvas_gsr.draw()
    appInstance.line_flex.set_data(appInstance.t_flex, numpy.array(list(appInstance.Flex)))
    appInstance.ax_flex.set_ylim(ymin=0, ymax=1024, auto=True)
    appInstance.graphCanvas_flex.draw()
    appInstance.line_emg1.set_data(appInstance.t_emg, numpy.array(list(appInstance.EMG1)))
    appInstance.line_emg2.set_data(appInstance.t_emg, numpy.array(list(appInstance.EMG2)))
    appInstance.ax_emg.set_ylim(ymin=0, ymax=1024, auto=True)
    appInstance.graphCanvas_emg.draw()

async def telemetryWrapper():
    try:
        appInstance.HR_Red.append(int.from_bytes(await appInstance.BLEDev.read_gatt_char(max_red_uuid), "little", signed=False))
        appInstance.HR_IR.append(int.from_bytes(await appInstance.BLEDev.read_gatt_char(max_ir_uuid), "little", signed=False))
        appInstance.GSR.append(int.from_bytes(await appInstance.BLEDev.read_gatt_char(gsr_uuid), "little", signed=False))
        appInstance.Flex.append(int.from_bytes(await appInstance.BLEDev.read_gatt_char(flex_uuid), "little", signed=False))
        appInstance.EMG1.append(int.from_bytes(await appInstance.BLEDev.read_gatt_char(emg1_uuid), "little", signed=False))
        appInstance.EMG2.append(int.from_bytes(await appInstance.BLEDev.read_gatt_char(emg2_uuid), "little", signed=False))
        await canvasWorker()
    except Exception as e:
        print(f"Data fetch failed, possibly because of device disconnect: {e}")
        pass

async def deviceScanWrapper():
    try:
        appInstance.connectSelectorList.set([devAddrFormat.format(dev_name=dev.name, uuid=dev.address) for dev in await bleak.BleakScanner.discover(timeout=2)])
    except OSError:
        messagebox.showerror(title="Bluetooth Adapter Exception", message="Check if bluetooth adapter is working.")
        destoryDialog()

async def tryConnectWrapper(devstr : str):
    devMatch = re.match(r"^(.*) \| (.+)$", devstr)
    if devMatch == None:
        return
    dev = bleak.BleakClient(devMatch.group(2))
    try:
        await dev.connect()
        appInstance.BLEDev = dev
        appInstance.cmdSendBtn.state(["!disabled"])
        appInstance.discBtn.state(["!disabled"])
    except asyncio.exceptions.TimeoutError as e:
        print(f"Timeout: {e}")
        appInstance.BLEDev = None

async def disconnectWrapper():
    if appInstance.BLEDev != None:
        await appInstance.BLEDev.disconnect()
        appInstance.BLEDev = None
        appInstance.cmdSendBtn.state(["disabled"])
        appInstance.discBtn.state(["disabled"])

async def connUpdateWrapper():
    if not appInstance.BLEDev.is_connected:
        await disconnectWrapper()

def onClose() -> None:
    appInstance.alive = False
    appInstance.evloop.stop()

def destoryDialog() -> None:
    appInstance.scanUpdate = False
    appInstance.connectDiag.grab_release()
    appInstance.connectDiag.destroy()
    appInstance.connectDiag = None
    appInstance.connectSelector = None
    appInstance.connectSelectorBtn = None
def createDialog() -> None:
    appInstance.connectDiag = tkinter.Toplevel(appInstance.mainWindow)
    appInstance.connectDiag.title(diagTitle)
    appInstance.connectDiag.resizable(FALSE, FALSE)
    frame = tkinter.Frame(appInstance.connectDiag)
    frame.grid()
    tkinter.Label(frame, text="Select sensor hub:").grid(row=0, column=1, columnspan=3)
    appInstance.connectSelectorList = tkinter.StringVar(frame, ["Please wait"])
    appInstance.connectSelector = tkinter.Listbox(frame, height=16, width=40, listvariable=appInstance.connectSelectorList)
    appInstance.connectSelector.grid(row=1, column=0, rowspan=3, columnspan=5)
    appInstance.connectSelectorBtn = tkinter.Button(frame, text="Connect", command=lambda: appInstance.tryConnect(eval(appInstance.connectSelectorList.get())[appInstance.connectSelector.curselection()[0]]) if appInstance.connectSelector.curselection() else appInstance.deviceScan())
    appInstance.connectSelectorBtn.grid(row=4, column=2)
    appInstance.scanUpdate = True
    appInstance.connectDiag.protocol("WM_DELETE_WINDOW", destoryDialog)
    appInstance.connectDiag.transient(appInstance.mainWindow)
    appInstance.connectDiag.wait_visibility()
    appInstance.connectDiag.grab_set()
    appInstance.deviceScan()
if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] != "-c":
            print("Usage: -c <MAC-ADDRESS>")
            exit()
        else:
            client = bleak.BleakClient(sys.argv[2])
            asyncio.run(client.connect())
            print(f"MTU is {client.mtu_size}, starting...")
            while True:
                val_max_red = int.from_bytes(asyncio.run(client.read_gatt_char(max_red_uuid)), "little", signed=False)
                val_max_ir = int.from_bytes(asyncio.run(client.read_gatt_char(max_ir_uuid)), "little", signed=False)
                val_gsr = int.from_bytes(asyncio.run(client.read_gatt_char(gsr_uuid)), "little", signed=False)
                val_flex = int.from_bytes(asyncio.run(client.read_gatt_char(flex_uuid)), "little", signed=False)
                val_emg1 = int.from_bytes(asyncio.run(client.read_gatt_char(emg1_uuid)), "little", signed=False)
                val_emg2 = int.from_bytes(asyncio.run(client.read_gatt_char(emg2_uuid)), "little", signed=False)
                print(f"Red: {val_max_red}, IR: {val_max_ir}, GSR: {val_gsr}, Flex: {val_flex}, EMG1: {val_emg1}, EMG2: {val_emg2}")
    else:
        appInstance = OpenTrackerApp()
        appInstance.evloop.run_forever()
        appInstance.evloop.close()
