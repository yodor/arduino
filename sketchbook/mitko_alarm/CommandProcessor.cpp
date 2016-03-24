#include "CommandProcessor.h"

CommandProcessor::CommandProcessor()
{
  have_error = true;
  sprintf(result, "%s", "");
}
CommandProcessor::~CommandProcessor()
{
}

void CommandProcessor::processLine(const String& line)
{

  unsigned int pos = 0;

  int found = -1;

  int group = 0;

  boolean parsing = true;

  while ( parsing ) {
    found = line.indexOf("|",pos);

    if (found == -1) {
      if (pos < line.length()) {
        found = line.length();
        parsing = false;
      }
      else {
        break;
      }
    }
    group++;

    String group_str = line.substring(pos, found);
    group_str.trim();

    processLineGroup(group, group_str);

    pos=found+1;
  }

  finishLine();
}

