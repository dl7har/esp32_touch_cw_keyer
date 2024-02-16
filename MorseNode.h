#include <stddef.h>

enum Symbol { NONE, DIT, DAH } ;

/**
 * writeSymbol() must be implemented by the caller of MorseNode->createSymbols()
 */
void writeSymbol(Symbol symbol);

struct MorseNode {
  char character;
  Symbol symbol;
  MorseNode* parent;
  MorseNode* left;
  MorseNode* right;

  void createSymbols() {
    internalCreateSymbols();
    createPauseSymbol();
  }

  private:
  void internalCreateSymbols() {
    if (getDepth() == 0) {
      return;
    }
    parent->internalCreateSymbols();
    writeSymbol(symbol);
  };
  void createPauseSymbol() {
    writeSymbol(NONE);
  }
  int getDepth() {
    if (parent == NULL) return 0;
    return 1 + parent->getDepth();
  };
};
MorseNode* findNode(MorseNode* node, char character);
MorseNode* newMorseTree();
