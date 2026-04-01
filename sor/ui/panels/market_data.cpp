#include "ui/panels/market_data.h"
#include "core/types.h"

namespace sor::ui
{

    MarketDataPanel::MarketDataPanel(SorController &controller)
        : controller_(controller) {}

    void MarketDataPanel::render()
    {
        ImGui::Begin("Market Data");

        // Add symbol input
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("##addsym", add_symbol_buf_, sizeof(add_symbol_buf_),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (add_symbol_buf_[0] != '\0')
            {
                controller_.add_watched_symbol(Symbol(add_symbol_buf_));
                add_symbol_buf_[0] = '\0';
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Add Symbol"))
        {
            if (add_symbol_buf_[0] != '\0')
            {
                controller_.add_watched_symbol(Symbol(add_symbol_buf_));
                add_symbol_buf_[0] = '\0';
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Show Depth", &show_depth_);

        ImGui::Separator();

        // NBBO table
        if (ImGui::BeginTable("nbbo", 10,
                              ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Symbol");
            ImGui::TableSetupColumn("Bid");
            ImGui::TableSetupColumn("BidQty");
            ImGui::TableSetupColumn("BidVenue");
            ImGui::TableSetupColumn("Ask");
            ImGui::TableSetupColumn("AskQty");
            ImGui::TableSetupColumn("AskVenue");
            ImGui::TableSetupColumn("Spread");
            ImGui::TableSetupColumn("Mid");
            ImGui::TableSetupColumn("Stale");
            ImGui::TableHeadersRow();

            for (const auto &sym : controller_.watched_symbols())
            {
                auto nbbo = controller_.get_nbbo(sym);
                bool stale = controller_.is_market_data_stale(sym);

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(sym.c_str());

                if (nbbo.valid())
                {
                    ImGui::TableNextColumn(); ImGui::TextColored({0.2f,1.0f,0.2f,1.0f}, "%.2f", to_double(nbbo.best_bid));
                    ImGui::TableNextColumn(); ImGui::Text("%ld", static_cast<long>(nbbo.best_bid_qty));
                    ImGui::TableNextColumn(); ImGui::Text("%u", nbbo.best_bid_venue);
                    ImGui::TableNextColumn(); ImGui::TextColored({1.0f,0.3f,0.3f,1.0f}, "%.2f", to_double(nbbo.best_ask));
                    ImGui::TableNextColumn(); ImGui::Text("%ld", static_cast<long>(nbbo.best_ask_qty));
                    ImGui::TableNextColumn(); ImGui::Text("%u", nbbo.best_ask_venue);
                    ImGui::TableNextColumn(); ImGui::Text("%.4f", to_double(nbbo.spread()));
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", to_double(nbbo.mid_price()));
                }
                else
                {
                    for (int i = 0; i < 8; ++i) { ImGui::TableNextColumn(); ImGui::Text("--"); }
                }

                ImGui::TableNextColumn();
                if (stale)
                    ImGui::TextColored({1.0f, 0.0f, 0.0f, 1.0f}, "STALE");
                else
                    ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "OK");
            }
            ImGui::EndTable();
        }

        // Optional depth view
        if (show_depth_)
        {
            for (const auto &sym : controller_.watched_symbols())
            {
                auto book = controller_.get_aggregated_book(sym);
                if (book.bid_depth == 0 && book.ask_depth == 0) continue;

                if (ImGui::TreeNode(sym.c_str()))
                {
                    if (ImGui::BeginTable("depth", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("BidQty");
                        ImGui::TableSetupColumn("BidPx");
                        ImGui::TableSetupColumn("AskPx");
                        ImGui::TableSetupColumn("AskQty");
                        ImGui::TableHeadersRow();

                        size_t max_levels = std::max(book.bid_depth, book.ask_depth);
                        for (size_t i = 0; i < max_levels && i < 10; ++i)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            if (i < book.bid_depth)
                                ImGui::Text("%ld", static_cast<long>(book.bids[i].total_quantity));
                            ImGui::TableNextColumn();
                            if (i < book.bid_depth)
                                ImGui::TextColored({0.2f,1.0f,0.2f,1.0f}, "%.2f", to_double(book.bids[i].price));
                            ImGui::TableNextColumn();
                            if (i < book.ask_depth)
                                ImGui::TextColored({1.0f,0.3f,0.3f,1.0f}, "%.2f", to_double(book.asks[i].price));
                            ImGui::TableNextColumn();
                            if (i < book.ask_depth)
                                ImGui::Text("%ld", static_cast<long>(book.asks[i].total_quantity));
                        }
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::End();
    }

} // namespace sor::ui
