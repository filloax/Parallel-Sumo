from io import TextIOBase
import threading
import sys 

class ThreadPrefixStream(TextIOBase):
    def __init__(self):
        self.thread_prefixes = {}
        self.buffers = {}

    def add_thread_prefix(self, prefix):
        self.thread_prefixes[threading.get_ident()] = prefix

    def write(self, text):
        thread_id = threading.get_ident()
        self.buffers[thread_id] = self.buffers.get(thread_id, '') + text
        if text == '\n':
            if thread_id in self.thread_prefixes:
                sys.__stdout__.write(f'{self.thread_prefixes[thread_id]} {self.buffers[thread_id]}')
            else:
                sys.__stdout__.write(self.buffers[thread_id])
            self.buffers[thread_id] = ''