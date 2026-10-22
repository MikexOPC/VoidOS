"""Second formatting pass after clang-format (user style, test.txt of 22 October 2026).

1. An if / else-if condition with a top-level || or && gets one operand per line, the operator
   at the end of the line, the closing parenthesis alone under the if, and the statement on
   the next line at the same indentation (a brace stays on the parenthesis line: ") {").
2. One empty line after a block that spans several lines (a line holding only "}" or the
   statement of a rewritten if), unless the next line already continues the construct.
Only code outside comments, strings, character literals and preprocessor lines is touched;
anything this pass cannot rewrite safely is left as clang-format produced it.
"""
import re
import sys
from pathlib import Path

# ====== code_mask ======

def code_mask(text):
    """True for characters that are ordinary code (not comment, string, char or preprocessor)."""
    mask=[False]*len(text); i=0; n=len(text); line_start=True
    while i<n:
        c=text[i]
        if line_start and c in ' \t':
            mask[i]=True; i+=1; continue
        if line_start and c=='#':
            while i<n and text[i]!='\n':
                if text[i]=='\\' and i+1<n and text[i+1]=='\n': i+=2; continue
                i+=1
            continue
        line_start=False
        if c=='\n': mask[i]=True; i+=1; line_start=True; continue
        if text.startswith('//',i):
            while i<n and text[i]!='\n': i+=1
            continue
        if text.startswith('/*',i):
            end=text.find('*/',i+2); i=n if end<0 else end+2; continue
        if c in '"\'':
            i+=1
            while i<n and text[i]!=c and text[i]!='\n':
                i+=2 if text[i]=='\\' else 1
            i+=1; continue
        mask[i]=True; i+=1
    return mask

# ====== match_paren ======

def match_paren(text,mask,open_at):
    depth=0
    for j in range(open_at,len(text)):
        if not mask[j]: continue
        if text[j] in '([{': depth+=1
        elif text[j] in ')]}':
            depth-=1
            if depth==0: return j if text[j]==')' else -1
    return -1

# ====== flat ======

def flat(fragment):
    return re.sub(r'\s*\n\s*',' ',fragment).strip()

# ====== rewrite_conditions ======

def rewrite_conditions(text):
    mask=code_mask(text); edits=[]; statement_lines=set(); i=0
    while True:
        i=text.find('if',i)
        if i<0: break
        start=i; i+=2
        if not mask[start] or (start and (text[start-1].isalnum() or text[start-1]=='_')) or \
           (start+2<len(text) and (text[start+2].isalnum() or text[start+2]=='_')): continue
        line_start=text.rfind('\n',0,start)+1
        prefix=text[line_start:start]
        if prefix.strip() not in ('','else'): continue
        indent=prefix[:len(prefix)-len(prefix.lstrip())]
        open_at=start+2
        while open_at<len(text) and text[open_at] in ' \t': open_at+=1
        if open_at>=len(text) or text[open_at]!='(': continue
        close=match_paren(text,mask,open_at)
        if close<0: continue
        inner=text[open_at+1:close]
        if '//' in inner or '/*' in inner: continue
        cuts=[]; depth=0
        for k in range(open_at+1,close):
            if not mask[k]: continue
            if text[k] in '([{': depth+=1
            elif text[k] in ')]}': depth-=1
            elif depth==0 and text[k:k+2] in ('||','&&') and mask[k+1]: cuts.append(k)
        if not cuts: continue
        pieces=[]; begin=open_at+1
        for cut in cuts:
            pieces.append((flat(text[begin:cut]),text[cut:cut+2])); begin=cut+2
        pieces.append((flat(text[begin:close]),''))
        if any(not operand for operand,_ in pieces): continue
        after=close+1
        while after<len(text) and text[after] in ' \t\n': after+=1
        if after>=len(text) or not mask[after]: continue
        if text[after]=='{':
            body=' {'; end=after+1
        else:
            if re.match(r'(if|for|while|do|switch|else|case|default)\b',text[after:]): continue
            semi=after; depth=0
            while semi<len(text):
                if mask[semi]:
                    if text[semi] in '([{': depth+=1
                    elif text[semi] in ')]}': depth-=1
                    elif text[semi]==';' and depth==0: break
                    if depth<0: break
                semi+=1
            statement=text[after:semi+1]
            if semi>=len(text) or depth<0 or '{' in statement or '//' in statement or '/*' in statement: continue
            body='\n'+indent+flat(statement); end=semi+1
        lines=['if ('+pieces[0][0]+' '+pieces[0][1]]
        lines+=[indent+'    '+operand+(' '+op if op else '') for operand,op in pieces[1:]]
        replacement='\n'.join(lines)+'\n'+indent+')'+body
        if edits and start<edits[-1][1]: continue
        edits.append((start,end,replacement,body!=' {'))
        i=end
    for start,end,replacement,_ in reversed(edits):
        text=text[:start]+replacement+text[end:]
    return text

# ====== blank_after_blocks ======

CONTINUES=('}','else','while','case ','default','#',')',']',';')

def blank_after_blocks(text):
    lines=text.split('\n'); out=[]
    for index,line in enumerate(lines):
        out.append(line)
        stripped=line.strip()
        ends_block=stripped=='}' or (stripped.startswith(')') and stripped.endswith('}'))
        rewritten=index>0 and lines[index-1].strip()==')' and stripped.endswith(';') and not stripped.startswith('}')
        if not (ends_block or rewritten) or index+1>=len(lines): continue
        following=lines[index+1].strip()
        if following and not following.startswith(CONTINUES) and line.startswith((' ','\t')): out.append('')
    return '\n'.join(out)

# ====== main ======

def main(paths):
    changed=0
    for name in paths:
        path=Path(name); original=path.read_text(encoding='utf-8')
        updated=blank_after_blocks(rewrite_conditions(original))
        if updated!=original:
            path.write_text(updated,encoding='utf-8',newline='\n'); changed+=1
    print(f'conditions.py: {changed} of {len(paths)} files updated')

if __name__=='__main__': main(sys.argv[1:])
