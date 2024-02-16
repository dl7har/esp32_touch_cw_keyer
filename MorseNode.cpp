#include <cctype>
#include <stdio.h>
#include "MorseNode.h"
//#define LOCAL

#ifdef LOCAL
void writeSymbol(Symbol symbol) {
  printf("%d", symbol);
}
#endif

MorseNode* findNode(MorseNode* node, char character) {
    if (node == NULL) {
      return NULL;
    }
    character = tolower(character);
    if (node->character == character) {
      return node;
    }
    MorseNode* found = findNode(node->left, character);
    if (found == NULL) {
      found = findNode(node->right, character);
    }
    return found;
}

MorseNode* newNode(char charVal, MorseNode* parent, Symbol symbolVal) {
    MorseNode* node = new MorseNode();
    node->character = charVal;
    node->symbol = symbolVal;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    if (parent != NULL) {
      if (symbolVal == DIT) {
        parent->left = node;
      }
      if (symbolVal == DAH) {
        parent->right = node;
      }
    }
    return (node);
}

MorseNode* newMorseTree() {

  // one symbol character
  MorseNode* root = newNode(' ', NULL, NONE);
  MorseNode* e = newNode('e', root, DIT);
  MorseNode* t = newNode('t', root, DAH);

  // two symbol character
  MorseNode* i = newNode('i', e, DIT);
  MorseNode* a = newNode('a', e, DAH);
  MorseNode* n = newNode('n', t, DIT);
  MorseNode* m = newNode('m', t, DAH);

  // three symbol character
  MorseNode* s = newNode('s', i, DIT);
  MorseNode* u = newNode('u', i, DAH);
  MorseNode* r = newNode('r', a, DIT);
  MorseNode* w = newNode('w', a, DAH);
  MorseNode* d = newNode('d', n, DIT);
  MorseNode* k = newNode('k', n, DAH);
  MorseNode* g = newNode('g', m, DIT);
  MorseNode* o = newNode('o', m, DAH);

  // four symbol character
  MorseNode* h = newNode('h', s, DIT);
  MorseNode* v = newNode('v', s, DAH);
  newNode('f', u, DIT);
  MorseNode* ue = newNode(0xFC, u, DAH);
  newNode('l', r, DIT);
  MorseNode* ae = newNode('l', r, DAH);
  newNode('p', w, DIT);
  MorseNode* j = newNode('j', w, DAH);
  MorseNode* b = newNode('b', d, DIT);
  MorseNode* x = newNode('x', d, DAH);
  MorseNode* c = newNode('c', k, DIT);
  newNode('y', k, DAH);
  MorseNode* z = newNode('z', g, DIT);
  newNode('q', g, DAH);
  MorseNode* oe = newNode(0xF6, o, DIT);
  MorseNode* ch = newNode(0xF7, o, DAH);

  // five symbol character
  newNode('5', h, DIT);
  newNode('4', h, DAH);
  newNode(0x06, v, DIT); // ACK -> VE verstanden
  newNode('3', v, DAH);
  newNode('1', j, DAH);
  newNode('6', b, DIT);
  newNode('/', x, DIT);
  newNode(0x02, c, DAH); // STX -> begin
  newNode('7', z, DIT);
  newNode('2', ue, DIT);
  MorseNode* end = newNode(0x03, ae, DIT); // ETX -> end
  newNode('8', oe, DIT);
  newNode('9', ch, DIT);

  // six symbol character
  newNode(',', newNode(',', z, DAH), DAH);
  newNode('.', end, DAH);
  newNode('?',  newNode(0xF8, ue, DIT), DIT);
  newNode('=', b, DAH);
  return root;
}

#ifdef LOCAL
int main() {
  printf("print ?\n");
  MorseNode* morseTree = newMorseTree();
  MorseNode* found = findNode(morseTree, '?');
  if (found != NULL) {
    found->createSymbols();
    printf("\nfound \n");
  } else {
    printf("not found");
  }
  printf("cq de\n");
  findNode(morseTree, 'c')->createSymbols();
  findNode(morseTree, 'q')->createSymbols();
  findNode(morseTree, ' ')->createSymbols();
  findNode(morseTree, 'd')->createSymbols();
  findNode(morseTree, 'e')->createSymbols();
  printf("\n done");
  return 0;
}
#endif
