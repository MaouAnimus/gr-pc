"""Local Windows debugger for failures that escape the in-process runner (not REA)."""
import ctypes as c
from ctypes import wintypes as w
import datetime as dt
import json
import os
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]
class Record(c.Structure):
    _fields_ = [('code', w.DWORD), ('flags', w.DWORD), ('record', c.c_void_p), ('address', c.c_void_p),
                ('count', w.DWORD), ('info', c.c_uint64 * 15)]
class ExceptionInfo(c.Structure):
    _fields_ = [('record', Record), ('first_chance', w.DWORD)]
class Payload(c.Union):
    _fields_ = [('exception', ExceptionInfo), ('raw', c.c_byte * 160)]
class Event(c.Structure):
    _fields_ = [('code', w.DWORD), ('process', w.DWORD), ('thread', w.DWORD), ('payload', Payload)]
class Startup(c.Structure):
    _fields_ = [('size', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
        ('x', w.DWORD), ('y', w.DWORD), ('width', w.DWORD), ('height', w.DWORD),
        ('charsx', w.DWORD), ('charsy', w.DWORD), ('fill', w.DWORD), ('flags', w.DWORD),
        ('show', w.WORD), ('reserved_size', w.WORD), ('reserved2', c.c_void_p),
        ('stdin', w.HANDLE), ('stdout', w.HANDLE), ('stderr', w.HANDLE)]
class Process(c.Structure):
    _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]


def main():
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD,
        c.c_void_p, w.LPCWSTR, c.POINTER(Startup), c.POINTER(Process)]
    kernel.WaitForDebugEvent.argtypes = [c.POINTER(Event), w.DWORD]
    kernel.ContinueDebugEvent.argtypes = [w.DWORD, w.DWORD, w.DWORD]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]; kernel.OpenThread.restype = w.HANDLE
    kernel.GetThreadContext.argtypes = [w.HANDLE, c.c_void_p]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    os.environ['PATH'] = r'C:\msys64\clang64\bin;' + os.environ['PATH']
    exe = ROOT / 'out/release/launcher.exe'
    startup, process = Startup(), Process(); startup.size = c.sizeof(Startup)
    command = c.create_unicode_buffer(f'"{exe}" --headless --boot --timeout=15000')
    if not kernel.CreateProcessW(str(exe), command, None, None, False, 2 | 0x08000000, None,
                                  str(ROOT), c.byref(startup), c.byref(process)):
        raise c.WinError(c.get_last_error())
    def memory(address, size):
        output = c.create_string_buffer(size); copied = c.c_size_t()
        kernel.ReadProcessMemory(process.process, address, output, size, c.byref(copied))
        return output.raw[:copied.value]
    reports = []
    deadline = time.monotonic() + 35
    while time.monotonic() < deadline:
        event = Event()
        if not kernel.WaitForDebugEvent(c.byref(event), 1000):
            continue
        disposition = 0x00010002
        if event.code == 1:
            record = event.payload.exception.record
            first = event.payload.exception.first_chance
            if record.code != 0x80000003:
                disposition = 0x80010001
            if record.code in (0xc0000005, 0xc000001d, 0xc00000fd):
                thread = kernel.OpenThread(0x0008, False, event.thread)
                # AMD64 CONTEXT is 1232 bytes, 16-byte aligned; flags at +0x30.
                storage = c.create_string_buffer(1232 + 16)
                address = (c.addressof(storage) + 15) & ~15
                c.c_uint32.from_address(address + 0x30).value = 0x100003
                context_ok = bool(kernel.GetThreadContext(thread, address))
                registers = {name: c.c_uint64.from_address(address + offset).value for name, offset in
                    [('rax', 0x78), ('rcx', 0x80), ('rdx', 0x88), ('rbx', 0x90), ('rsp', 0x98),
                     ('rbp', 0xa0), ('rsi', 0xa8), ('rdi', 0xb0), ('rip', 0xf8)]} if context_ok else {}
                item = {'code': hex(record.code), 'first_chance': first, 'thread': event.thread,
                    'address': hex(record.address or 0), 'info': list(record.info)[:record.count],
                    'registers': {key: hex(value) for key, value in registers.items()},
                    'instruction': memory(record.address or 0, 48).hex(),
                    'stack': memory(registers.get('rsp', 0), 256).hex()}
                kernel.CloseHandle(thread)
                reports.append(item); print(json.dumps(item), flush=True)
            if not first and record.code != 0x80000003:
                print('Unhandled guest/native exception captured', flush=True)
        if event.code in (3, 6):  # CREATE_PROCESS/LOAD_DLL: close the image-file handle.
            handle = c.c_void_p.from_buffer(event.payload).value
            if handle: kernel.CloseHandle(handle)
        kernel.ContinueDebugEvent(event.process, event.thread, disposition)
        if event.code == 5:
            break
    else:
        kernel.TerminateProcess(process.process, 124)
    kernel.CloseHandle(process.thread); kernel.CloseHandle(process.process)
    path = ROOT / 'out/evidence' / ('debug_' + dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '.json')
    path.write_text(json.dumps(reports, indent=2), encoding='utf-8')
    print('Debugger evidence: ' + str(path))


if __name__ == '__main__':
    main()
