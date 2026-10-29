"""Halo PS3 Stereo + Move: source patch installer and portable settings editor."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import subprocess
import sys

HERE = Path(__file__).resolve().parent

def patch_source(folder, apply=False):
    folder = Path(folder).resolve()
    if not (folder / 'port/ps3/src/ps3_video.c').is_file() or not (folder / 'Makefile').is_file():
        raise ValueError('Select a halo-ce-ps3 source checkout, containing Makefile and port/ps3/.')
    manifest = json.loads((HERE / 'extension-manifest.json').read_text())
    patch = HERE / 'halo-stereo-move.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != manifest['patch_sha256']:
        raise ValueError('Patch checksum does not match the manifest.')
    def git(*args):
        return subprocess.run(['git', '-C', str(folder), 'apply', '--whitespace=nowarn', *args, str(patch)],
                              capture_output=True, text=True, creationflags=0x08000000 if sys.platform=='win32' else 0)
    result = git('--check')
    if result.returncode:
        if git('--reverse', '--check').returncode == 0:
            return 'This source already contains the extension patch.'
        raise ValueError('This source differs from the supported patch context. No files changed.\n'
                         'Rebase the extension against this release before building.\n\n' + result.stderr)
    if apply:
        result = git()
        if result.returncode:
            raise RuntimeError(result.stderr)
        return ('Source patch applied. Build and run the link checks:\n'
                './build.sh -j12 BUILD=build/extensions pkg check\n./build.sh BUILD=build/extensions self\n\n'
                'Install the rebuilt PKG. A later stock PKG replaces the extension executable; '
                'build a matching extension update for each upstream release.')
    return 'Compatible: git apply --check passed. No files changed.'

def gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    root = tk.Tk();root.title('Halo PS3 — Stereo + Move Extension Manager');root.geometry('900x650')
    notebook=ttk.Notebook(root);notebook.pack(fill='both',expand=True,padx=12,pady=12)
    source=ttk.Frame(notebook);settings_page=ttk.Frame(notebook)
    notebook.add(source,text='Patch future source');notebook.add(settings_page,text='PS3 settings')
    ttk.Label(source,text='Apply the extension to a compatible Halo PS3 source release, then rebuild its PKG.',wraplength=820).pack(anchor='w',padx=12,pady=12)
    ttk.Label(source,text='The patcher checks compatibility before writing. Binary PKGs require a rebuilt executable.',wraplength=820).pack(anchor='w',padx=12)
    folder=tk.StringVar();row=ttk.Frame(source);row.pack(fill='x',padx=12,pady=16)
    ttk.Entry(row,textvariable=folder).pack(side='left',fill='x',expand=True)
    ttk.Button(row,text='Source folder…',command=lambda:folder.set(filedialog.askdirectory() or folder.get())).pack(side='left',padx=8)
    output=tk.Text(source,wrap='word',height=20);output.pack(fill='both',expand=True,padx=12,pady=12)
    def run(apply):
        try:
            if not folder.get():raise ValueError('Select the source folder first.')
            message=patch_source(folder.get(),apply)
        except Exception as exc:message=str(exc)
        output.delete('1.0','end');output.insert('1.0',message)
    buttons=ttk.Frame(source);buttons.pack(anchor='w',padx=12,pady=12)
    ttk.Button(buttons,text='Check compatibility',command=lambda:run(False)).pack(side='left')
    ttk.Button(buttons,text='Apply source patch',command=lambda:run(True)).pack(side='left',padx=12)
    schema=json.loads((HERE/'settings.json').read_text());values={s['name']:s['default'] for s in schema};extra={}
    ttk.Label(settings_page,text='Open extensions.cfg copied from your PS3, or edit defaults. Copy the saved file to /dev_hdd0/data/HaloCE/.',wraplength=840).pack(anchor='w',padx=12,pady=12)
    columns=('setting','value');tree=ttk.Treeview(settings_page,columns=columns,show='headings',selectmode='browse')
    tree.heading('setting',text='Setting');tree.heading('value',text='Value');tree.column('setting',width=400);tree.column('value',width=140)
    scroll=ttk.Scrollbar(settings_page,command=tree.yview);tree.configure(yscrollcommand=scroll.set)
    scroll.pack(side='right',fill='y');tree.pack(fill='both',expand=True,padx=12)
    description=tk.StringVar();ttk.Label(settings_page,textvariable=description,wraplength=820).pack(fill='x',padx=12,pady=8)
    new_value=tk.StringVar();editrow=ttk.Frame(settings_page);editrow.pack(fill='x',padx=12)
    ttk.Entry(editrow,textvariable=new_value).pack(side='left',fill='x',expand=True)
    def refresh():
        tree.delete(*tree.get_children())
        for s in schema:tree.insert('', 'end',iid=s['name'],values=(s['name'],values[s['name']]))
    def selected(event=None):
        choice=tree.selection()
        if choice:
            key=choice[0];new_value.set(values[key]);description.set(next(s['description'] for s in schema if s['name']==key))
    tree.bind('<<TreeviewSelect>>',selected)
    def set_value():
        if not tree.selection():return
        try:
            value=new_value.get().strip()
            if not math.isfinite(float(value)):raise ValueError('Use a finite numeric value.')
            key=tree.selection()[0];values[key]=value;tree.item(key,values=(key,value))
        except ValueError as exc:messagebox.showerror('Setting',str(exc))
    ttk.Button(editrow,text='Set value',command=set_value).pack(side='left',padx=8)
    def load():
        path=filedialog.askopenfilename(filetypes=[('Extension settings','*.cfg'),('All files','*')])
        if not path:return
        try:
            for line in Path(path).read_text().splitlines():
                if not line.strip() or line.lstrip().startswith('#'):continue
                pair=line.split(None,1)
                if len(pair)!=2:continue
                key,value=pair
                if key in values and math.isfinite(float(value)):values[key]=value
                else:extra[key]=value
            refresh()
        except Exception as exc:messagebox.showerror('Open settings',str(exc))
    def save():
        path=filedialog.asksaveasfilename(initialfile='extensions.cfg',defaultextension='.cfg',filetypes=[('Extension settings','*.cfg')])
        if path:
            Path(path).write_text('# Halo PS3 Stereo + Move extension\n'+''.join(f'{k} {v}\n' for k,v in (values|extra).items()),encoding='utf-8')
    buttons=ttk.Frame(settings_page);buttons.pack(anchor='w',padx=12,pady=12)
    ttk.Button(buttons,text='Open settings…',command=load).pack(side='left')
    ttk.Button(buttons,text='Save settings…',command=save).pack(side='left',padx=12)
    refresh();root.mainloop()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    group=parser.add_mutually_exclusive_group();group.add_argument('--check',metavar='SOURCE');group.add_argument('--apply',metavar='SOURCE')
    args=parser.parse_args()
    if args.check or args.apply:
        try:print(patch_source(args.check or args.apply,bool(args.apply)))
        except Exception as exc:print(str(exc),file=sys.stderr);sys.exit(1)
    else:gui()
