#pragma once

namespace epd
{
// The driver owns the attempt budget. Every successful preparation has reset
// controller RAM, so always replay pages from firstPage(), never resume a page.
template<typename Display, typename DrawPage>
bool renderPagesWithRecovery(Display& display, const DrawPage& drawPage)
{
  while (display.epd2.prepareUpdate())
  {
    display.firstPage();
    bool more;
    do
    {
      drawPage();
      more = display.nextPage();
    } while (more && display.epd2.healthy());
    if (display.epd2.healthy()) return true;
  }
  return false;
}
} // namespace epd
